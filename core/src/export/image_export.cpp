#include "canvas/core/export/image_export.hpp"

#include "canvas/core/export/deliver_preset.hpp"
#include "canvas/core/export/renderer.hpp"
#include "canvas/core/project/project.hpp"
#include "canvas/core/util/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace canvas::core {

namespace {

int channels_for(const AVPixelFormat fmt) {
    return fmt == AV_PIX_FMT_RGBA ? 4 : 3;
}

std::string write_error(const std::string& path) {
    return "Cannot write image: " + path;
}

struct PngEncoder {
    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    int64_t pts = 0;

    PngEncoder() = default;
    PngEncoder(const PngEncoder&) = delete;
    PngEncoder& operator=(const PngEncoder&) = delete;
    ~PngEncoder() { close(); }

    void close() {
        if (frame) av_frame_free(&frame);
        if (ctx) avcodec_free_context(&ctx);
    }

    [[nodiscard]] bool open(const int w, const int h) {
        const AVCodec* codec = avcodec_find_encoder_by_name("png");
        if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
        if (!codec) return false;
        const AVPixelFormat tries[] = {AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24};
        for (const AVPixelFormat fmt : tries) {
            close();
            ctx = avcodec_alloc_context3(codec);
            if (!ctx) return false;
            ctx->width = w;
            ctx->height = h;
            ctx->pix_fmt = fmt;
            ctx->time_base = AVRational{1, 100};
            if (avcodec_open2(ctx, codec, nullptr) < 0) continue;
            frame = av_frame_alloc();
            if (!frame) {
                close();
                return false;
            }
            frame->format = ctx->pix_fmt;
            frame->width = w;
            frame->height = h;
            if (av_frame_get_buffer(frame, 32) < 0) {
                close();
                return false;
            }
            pts = 0;
            return true;
        }
        close();
        return false;
    }

    [[nodiscard]] bool write(const std::string& path, const VideoFrame& vf, std::string* error) {
        if (!ctx || !frame) {
            if (error) *error = "PNG encoder is not open.";
            return false;
        }
        if (vf.width != ctx->width || vf.height != ctx->height ||
            vf.stride < static_cast<std::size_t>(vf.width) * 4u || vf.rgba.empty()) {
            if (error)
                *error = "Rendered frame does not match the export resolution.";
            return false;
        }
        if (av_frame_make_writable(frame) < 0) {
            if (error) *error = "PNG frame buffer is not writable.";
            return false;
        }
        const int channels = channels_for(ctx->pix_fmt);
        const std::size_t row_bytes = static_cast<std::size_t>(vf.width) *
                                      static_cast<std::size_t>(channels);
        for (int y = 0; y < vf.height; ++y) {
            const uint8_t* src = vf.rgba.data() + static_cast<std::size_t>(y) * vf.stride;
            std::memcpy(frame->data[0] + static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(frame->linesize[0]),
                        src, row_bytes);
        }
        frame->pts = pts++;
        if (avcodec_send_frame(ctx, frame) < 0) {
            if (error) *error = "PNG encoder rejected the frame.";
            return false;
        }
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            if (error) *error = "Out of memory encoding PNG.";
            return false;
        }
        bool wrote = false;
        int ret = 0;
        while ((ret = avcodec_receive_packet(ctx, pkt)) == 0) {
            FILE* f = std::fopen(path.c_str(), "wb");
            if (!f) {
                av_packet_free(&pkt);
                if (error) *error = write_error(path);
                return false;
            }
            const std::size_t n = std::fwrite(pkt->data, 1, static_cast<std::size_t>(pkt->size), f);
            const bool ok = n == static_cast<std::size_t>(pkt->size) && std::fclose(f) == 0;
            av_packet_unref(pkt);
            if (!ok) {
                av_packet_free(&pkt);
                if (error) *error = write_error(path);
                return false;
            }
            wrote = true;
        }
        av_packet_free(&pkt);
        if (!wrote) {
            if (error) *error = "PNG encode produced no image data.";
            return false;
        }
        return true;
    }
};

void derive_size(const Project& project, int* w, int* h) {
    int mw = 0;
    int mh = 0;
    for (const auto& track : project.sequence.video_tracks) {
        for (const auto& clip : track.clips) {
            if (!clip.enabled) continue;
            const MediaEntry* m = project.media_by_id(clip.media);
            if (!m) continue;
            if (m->width > mw && m->height > mh) {
                mw = m->width;
                mh = m->height;
            }
        }
    }
    *w = mw > 0 ? mw : 1920;
    *h = mh > 0 ? mh : 1080;
}

}

bool export_image_frames(const Project& project, const ExportSettings& s, ExportControl* control,
                         std::string* error) {
    const auto fail = [&](const std::string& m) {
        ::canvas::core::log::log_error("image export: %s", m.c_str());
        if (error) *error = m;
        return false;
    };
    const auto cancelled = [&]() {
        return control && control->should_cancel && control->should_cancel();
    };
    const auto report = [&](const double p) {
        if (control && control->on_progress) control->on_progress(p, "Encode");
    };

    if (s.output_path.empty()) return fail("Output path is missing.");
    const bool still = scope_is_still(s.render_scope);
    if (!still && !scope_is_sequence(s.render_scope))
        return fail("Render scope is neither a still nor a frame sequence.");

    int w = s.width;
    int h = s.height;
    if (w <= 0 || h <= 0) derive_size(project, &w, &h);
    if (w <= 0 || h <= 0) return fail("Export resolution is invalid.");

    const int64_t start = s.start_frame > 0 ? s.start_frame : 0;
    const int64_t count = still ? 1 : std::max<int64_t>(1, s.duration_frames);
    const double seq_fps = project.sequence.fps;
    const double export_fps = s.fps > 0.0 ? s.fps : (seq_fps > 0.0 ? seq_fps : 30.0);
    const double tl_per_frame =
        (seq_fps > 0.0 && export_fps > 0.0) ? seq_fps / export_fps : 1.0;

    CANVAS_LOG("image export: scope=%s out='%s' %dx%d frames=%lld start=%lld",
               still ? "still" : "sequence", s.output_path.c_str(), w, h,
               (long long)count, (long long)start);

    RenderSession session;
    if (!session.begin(project, w, h)) return fail("Failed to begin the render session.");

    bool ok = true;
    std::string why;
    PngEncoder enc;
    report(0.0);
    for (int64_t i = 0; i < count && ok; ++i) {
        if (cancelled()) {
            ok = false;
            why = "Cancelled.";
            break;
        }
        const int64_t tl =
            start + static_cast<int64_t>(std::llround(static_cast<double>(i) * tl_per_frame));
        VideoFramePtr vf = session.frame(tl);
        if (!vf) {
            ok = false;
            why = "Failed to render frame at timeline position " + std::to_string(tl) + ".";
            break;
        }
        if (!enc.ctx && !enc.open(vf->width, vf->height)) {
            ok = false;
            why = "No PNG encoder is available in this FFmpeg build.";
            break;
        }
        const std::string path = still ? still_output_path(s.output_path)
                                       : sequence_output_path(s.output_path, i + 1);
        if (!enc.write(path, *vf, &why)) {
            ok = false;
            break;
        }
        if (control && control->on_frame) control->on_frame(vf);
        report(static_cast<double>(i + 1) / static_cast<double>(count));
    }
    session.end();

    if (!ok) return fail(why.empty() ? "Image export failed." : why);
    CANVAS_LOG("image export: wrote %lld file(s) from '%s'", (long long)count,
               s.output_path.c_str());
    return true;
}

}
