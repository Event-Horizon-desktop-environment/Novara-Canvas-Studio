#include "canvas/core/export/deliver_preset.hpp"
#include "canvas/core/export/exporter.hpp"
#include "canvas/core/export/image_export.hpp"
#include "canvas/core/export/renderer.hpp"
#include "canvas/core/project/project.hpp"
#include "canvas/core/timeline/edit_ops.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

using namespace canvas::core;

namespace {

int failures = 0;

void check(const bool cond, const char* what) {
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

const char* kOutDir = "/tmp/canvas_image_export";

bool make_source(const std::string& path, const int w, const int h, const int fps,
                 const int frames) {
    const AVCodec* codec = avcodec_find_encoder_by_name("ffv1");
    if (!codec) return false;
    AVFormatContext* oc = nullptr;
    avformat_alloc_output_context2(&oc, nullptr, "avi", path.c_str());
    if (!oc) return false;

    AVStream* st = avformat_new_stream(oc, nullptr);
    AVCodecContext* c = st ? avcodec_alloc_context3(codec) : nullptr;
    if (!c) {
        avformat_free_context(oc);
        return false;
    }
    c->width = w;
    c->height = h;
    c->time_base = AVRational{1, fps};
    st->time_base = c->time_base;
    c->framerate = AVRational{fps, 1};
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->gop_size = 12;
    c->max_b_frames = 0;
    if (avcodec_open2(c, codec, nullptr) < 0) {
        avcodec_free_context(&c);
        avformat_free_context(oc);
        return false;
    }
    if (avcodec_parameters_from_context(st->codecpar, c) < 0) {
        avcodec_free_context(&c);
        avformat_free_context(oc);
        return false;
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (avformat_write_header(oc, nullptr) < 0) {
        avcodec_free_context(&c);
        avformat_free_context(oc);
        return false;
    }

    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width = w;
    f->height = h;
    av_frame_get_buffer(f, 32);

    for (int n = 0; n < frames; ++n) {
        av_frame_make_writable(f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                f->data[0][y * f->linesize[0] + x] = static_cast<uint8_t>((x + y + n * 16) & 0xff);
        for (int y = 0; y < h / 2; ++y)
            for (int x = 0; x < w / 2; ++x) {
                f->data[1][y * f->linesize[1] + x] = 128;
                f->data[2][y * f->linesize[2] + x] = 128;
            }
        f->pts = n;
        avcodec_send_frame(c, f);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(c, pkt) == 0) {
            av_packet_rescale_ts(pkt, c->time_base, st->time_base);
            pkt->stream_index = st->index;
            av_interleaved_write_frame(oc, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
    av_frame_free(&f);
    avcodec_send_frame(c, nullptr);
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(c, pkt) == 0) {
        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        pkt->stream_index = st->index;
        av_interleaved_write_frame(oc, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_write_trailer(oc);
    avcodec_free_context(&c);
    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    avformat_free_context(oc);
    return true;
}

Project make_project(const std::string& src_path) {
    Project p;
    p.name = "ImageExport";
    p.sequence.fps = 30.0;
    MediaEntry m;
    m.id = 0;
    m.path = src_path;
    m.fps = 30.0;
    m.width = 128;
    m.height = 96;
    m.total_frames = 12;
    p.media.push_back(m);
    Track v;
    v.kind = Track::Kind::Video;
    v.name = "V1";
    p.sequence.video_tracks.push_back(std::move(v));
    Clip clip;
    clip.media = 0;
    clip.name = "A";
    clip.tl_in = 0;
    clip.src_in = 0;
    clip.src_out = 12;
    clip.title.text = "STILL";
    clip.title.size = 0.16f;
    clip.title.bold = true;
    clip.pos_y = 25.0;
    place_clip(p.sequence, Track::Kind::Video, 0, clip, Placement::Overwrite);
    return p;
}

long long file_size(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fclose(f);
    return sz;
}

bool has_png_signature(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char sig[8] = {0};
    const std::size_t n = std::fread(sig, 1, sizeof(sig), f);
    std::fclose(f);
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return n == sizeof(sig) && std::memcmp(sig, kSig, sizeof(sig)) == 0;
}

bool decode_rgba(const std::string& path, const int expect_w, const int expect_h,
                 std::vector<uint8_t>* out, int* out_w, int* out_h) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    AVStream* st = fmt->streams[idx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
        avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }

    AVFrame* f = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    bool got = false;
    while (!got && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx && avcodec_send_packet(ctx, pkt) == 0) {
            if (avcodec_receive_frame(ctx, f) == 0) got = true;
        }
        av_packet_unref(pkt);
    }
    if (!got) {
        av_packet_free(&pkt);
        av_frame_free(&f);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }

    const int w = f->width;
    const int h = f->height;
    const bool sized = w == expect_w && h == expect_h;
    std::vector<uint8_t> buf(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0);
    uint8_t* dst[4] = {buf.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {w * 4, 0, 0, 0};
    SwsContext* sws =
        sws_getContext(w, h, static_cast<AVPixelFormat>(f->format), w, h, AV_PIX_FMT_RGBA,
                       SWS_POINT, nullptr, nullptr, nullptr);
    if (sws) {
        sws_scale(sws, f->data, f->linesize, 0, h, dst, dst_linesize);
        sws_freeContext(sws);
    } else {
        got = false;
    }

    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    if (!got || !sized) return false;
    *out = std::move(buf);
    *out_w = w;
    *out_h = h;
    return true;
}

ExportSettings base_settings(const std::string& out_path) {
    ExportSettings s;
    s.output_path = out_path;
    s.format = "png";
    s.video_codec = "png";
    s.width = 128;
    s.height = 96;
    s.fps = 30.0;
    s.duration_frames = 12;
    s.remove_audio = true;
    s.audio_codec.clear();
    return s;
}

void test_still(const Project& p) {
    const std::string path = std::string(kOutDir) + "/still.png";
    ExportSettings s = base_settings(path);
    s.render_scope = RenderScope::Still;
    s.start_frame = 5;

    std::string error;
    const bool ok = export_image_frames(p, s, nullptr, &error);
    check(ok, "still export succeeds");
    if (!ok) std::printf("      error: %s\n", error.c_str());
    check(has_png_signature(path), "still file has a PNG signature");
    check(file_size(path) > 64, "still file is not empty");

    std::vector<uint8_t> got;
    int w = 0;
    int h = 0;
    const bool decoded = decode_rgba(path, 128, 96, &got, &w, &h);
    check(decoded, "still decodes at the export resolution");
    if (!decoded) return;

    VideoFramePtr ref;
    {
        RenderSession sess;
        if (sess.begin(p, 128, 96)) {
            ref = sess.frame(5);
            sess.end();
        }
    }
    check(ref != nullptr, "reference frame renders");
    if (!ref) return;
    check(ref->width == 128 && ref->height == 96, "reference frame matches export size");

    bool alpha_opaque = true;
    std::size_t mismatches = 0;
    for (int y = 0; y < 96; ++y) {
        const uint8_t* rf = ref->rgba.data() + static_cast<std::size_t>(y) * ref->stride;
        const uint8_t* gf = got.data() + static_cast<std::size_t>(y) * 128u * 4u;
        for (int x = 0; x < 128; ++x) {
            const std::size_t o = static_cast<std::size_t>(x) * 4u;
            if (rf[o + 3] != 255) alpha_opaque = false;
            if (rf[o] != gf[o] || rf[o + 1] != gf[o + 1] || rf[o + 2] != gf[o + 2] ||
                rf[o + 3] != gf[o + 3])
                ++mismatches;
        }
    }
    check(mismatches == 0, "still pixels match the reference render exactly");
    check(alpha_opaque, "reference render is fully opaque");
}

void test_sequence(const Project& p) {
    const std::string base = std::string(kOutDir) + "/seq.png";
    ExportSettings s = base_settings(base);
    s.render_scope = RenderScope::FrameSequence;
    s.duration_frames = 5;

    std::string error;
    const bool ok = export_image_frames(p, s, nullptr, &error);
    check(ok, "sequence export succeeds");
    if (!ok) std::printf("      error: %s\n", error.c_str());

    int written = 0;
    int decodable = 0;
    for (int64_t i = 1; i <= 5; ++i) {
        const std::string path = sequence_output_path(base, i);
        if (file_size(path) > 64 && has_png_signature(path)) ++written;
        std::vector<uint8_t> px;
        int w = 0;
        int h = 0;
        if (decode_rgba(path, 128, 96, &px, &w, &h)) ++decodable;
    }
    check(written == 5, "sequence writes 5 numbered frames");
    check(decodable == 5, "every sequence frame decodes");
    check(file_size(sequence_output_path(base, 6)) < 0, "sequence stops at the requested count");
    check(file_size(std::string(kOutDir) + "/seq.png") < 0, "sequence writes no bare output file");
}

void test_sequence_progress(const Project& p) {
    const std::string base = std::string(kOutDir) + "/prog.png";
    ExportSettings s = base_settings(base);
    s.render_scope = RenderScope::FrameSequence;
    s.duration_frames = 4;
    ExportControl ctrl;
    double last = -1.0;
    bool monotonic = true;
    ctrl.on_progress = [&](double progress, const std::string&) {
        if (progress < last) monotonic = false;
        last = progress;
    };
    std::string error;
    const bool ok = export_image_frames(p, s, &ctrl, &error);
    check(ok, "progress-reporting sequence succeeds");
    check(monotonic && last > 0.0, "progress advances to completion");
    check(last >= 0.99, "final progress reports 100%");
}

void test_cancel(const Project& p) {
    const std::string base = std::string(kOutDir) + "/cancel.png";
    ExportSettings s = base_settings(base);
    s.render_scope = RenderScope::FrameSequence;
    s.duration_frames = 6;
    ExportControl ctrl;
    ctrl.should_cancel = [] { return true; };
    std::string error;
    const bool ok = export_image_frames(p, s, &ctrl, &error);
    check(!ok, "cancelled image export fails");
    check(error.find("Cancel") != std::string::npos, "cancellation reports a cancel error");
    check(file_size(sequence_output_path(base, 1)) < 0, "cancelled export writes no frames");
}

void test_scope_guard(const Project& p) {
    ExportSettings s = base_settings(std::string(kOutDir) + "/guard.mkv");
    s.render_scope = RenderScope::SingleClip;
    std::string error;
    check(!export_image_frames(p, s, nullptr, &error),
          "a video scope is rejected by the image exporter");
    check(!error.empty(), "scope rejection reports why");
}

}

int main() {
    std::filesystem::remove_all(kOutDir);
    std::filesystem::create_directories(kOutDir);

    const std::string src = std::string(kOutDir) + "/source.avi";
    if (!make_source(src, 128, 96, 30, 12)) {
        std::printf("SKIP  no ffv1 encoder to synthesize source; cannot run image export\n");
        return 2;
    }
    const Project p = make_project(src);

    test_still(p);
    test_sequence(p);
    test_sequence_progress(p);
    test_cancel(p);
    test_scope_guard(p);

    if (failures == 0) {
        std::printf("ALL IMAGE EXPORT TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
