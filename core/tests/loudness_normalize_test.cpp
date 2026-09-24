#include "canvas/core/export/exporter.hpp"
#include "canvas/core/export/loudness.hpp"
#include "canvas/core/project/project.hpp"
#include "canvas/core/timeline/edit_ops.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
}

using namespace canvas::core;

namespace {

int failures = 0;

void check(const bool cond, const char* what) {
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

void check_near(const float got, const float want, const float tol, const char* what) {
    const bool ok = std::fabs(got - want) <= tol;
    std::printf("%s  %s (got %.2f want %.2f +-%.2f)\n", ok ? "PASS" : "FAIL", what,
                static_cast<double>(got), static_cast<double>(want), static_cast<double>(tol));
    if (!ok) ++failures;
}

const char* kOutDir = "/tmp/canvas_loudness_normalize";
constexpr double kSrcLufs = -23.0103;
constexpr float kTargetLufs = -20.0f;

bool make_source(const std::string& path, const int w, const int h, const int fps,
                 const int frames, const int sample_rate) {
    const AVCodec* vcodec = avcodec_find_encoder_by_name("ffv1");
    const AVCodec* acodec = avcodec_find_encoder_by_name("pcm_s16le");
    if (!vcodec || !acodec) return false;

    AVFormatContext* oc = nullptr;
    avformat_alloc_output_context2(&oc, nullptr, "matroska", path.c_str());
    if (!oc) return false;

    AVStream* vst = avformat_new_stream(oc, nullptr);
    AVCodecContext* vctx = vst ? avcodec_alloc_context3(vcodec) : nullptr;
    if (!vctx) {
        avformat_free_context(oc);
        return false;
    }
    vctx->width = w;
    vctx->height = h;
    vctx->time_base = AVRational{1, fps};
    vctx->framerate = AVRational{fps, 1};
    vctx->pix_fmt = AV_PIX_FMT_YUV420P;
    vctx->gop_size = 12;
    vctx->max_b_frames = 0;
    if (avcodec_open2(vctx, vcodec, nullptr) < 0 || avcodec_parameters_from_context(vst->codecpar, vctx) < 0) {
        avcodec_free_context(&vctx);
        avformat_free_context(oc);
        return false;
    }
    vst->time_base = vctx->time_base;

    AVStream* ast = avformat_new_stream(oc, nullptr);
    AVCodecContext* actx = ast ? avcodec_alloc_context3(acodec) : nullptr;
    if (!actx) {
        avcodec_free_context(&vctx);
        avformat_free_context(oc);
        return false;
    }
    actx->sample_rate = sample_rate;
    actx->sample_fmt = AV_SAMPLE_FMT_S16;
    actx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    actx->time_base = AVRational{1, sample_rate};
    if (avcodec_open2(actx, acodec, nullptr) < 0 ||
        avcodec_parameters_from_context(ast->codecpar, actx) < 0) {
        avcodec_free_context(&actx);
        avcodec_free_context(&vctx);
        avformat_free_context(oc);
        return false;
    }
    ast->time_base = actx->time_base;

    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (avformat_write_header(oc, nullptr) < 0) {
        avcodec_free_context(&actx);
        avcodec_free_context(&vctx);
        avformat_free_context(oc);
        return false;
    }

    AVFrame* vf = av_frame_alloc();
    vf->format = AV_PIX_FMT_YUV420P;
    vf->width = w;
    vf->height = h;
    av_frame_get_buffer(vf, 32);
    AVFrame* af = av_frame_alloc();
    af->format = AV_SAMPLE_FMT_S16;
    af->sample_rate = sample_rate;
    af->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    af->nb_samples = 1024;
    av_frame_get_buffer(af, 0);

    const double tone_hz = 1000.0;
    const double amplitude = 0.1;
    int64_t sample_cursor = 0;
    const int64_t total_samples = (int64_t)((double)frames / (double)fps * (double)sample_rate);

    for (int n = 0; n < frames; ++n) {
        av_frame_make_writable(vf);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                vf->data[0][y * vf->linesize[0] + x] = static_cast<uint8_t>((x + y + n * 8) & 0xff);
        for (int y = 0; y < h / 2; ++y)
            for (int x = 0; x < w / 2; ++x) {
                vf->data[1][y * vf->linesize[1] + x] = 128;
                vf->data[2][y * vf->linesize[2] + x] = 128;
            }
        vf->pts = n;
        avcodec_send_frame(vctx, vf);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(vctx, pkt) == 0) {
            av_packet_rescale_ts(pkt, vctx->time_base, vst->time_base);
            pkt->stream_index = vst->index;
            av_interleaved_write_frame(oc, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);

        while (sample_cursor < total_samples) {
            av_frame_make_writable(af);
            const int want = af->nb_samples;
            for (int i = 0; i < want; ++i) {
                const int64_t s = sample_cursor + i;
                const double t = (double)s / (double)sample_rate;
                const float v = (float)(amplitude * std::sin(2.0 * std::numbers::pi * tone_hz * t));
                auto* d = reinterpret_cast<int16_t*>(af->data[0]);
                d[i * 2] = (int16_t)(v * 32767.0f);
                d[i * 2 + 1] = d[i * 2];
            }
            af->pts = sample_cursor;
            const int64_t take = (total_samples - sample_cursor) < want
                                     ? (total_samples - sample_cursor)
                                     : want;
            sample_cursor += take;
            avcodec_send_frame(actx, af);
            AVPacket* ap = av_packet_alloc();
            while (avcodec_receive_packet(actx, ap) == 0) {
                av_packet_rescale_ts(ap, actx->time_base, ast->time_base);
                ap->stream_index = ast->index;
                av_interleaved_write_frame(oc, ap);
                av_packet_unref(ap);
            }
            av_packet_free(&ap);
            if (sample_cursor >= total_samples) break;
        }
        if (sample_cursor >= total_samples && n + 1 < frames) {
            avcodec_send_frame(vctx, nullptr);
            AVPacket* p2 = av_packet_alloc();
            while (avcodec_receive_packet(vctx, p2) == 0) {
                av_packet_rescale_ts(p2, vctx->time_base, vst->time_base);
                p2->stream_index = vst->index;
                av_interleaved_write_frame(oc, p2);
                av_packet_unref(p2);
            }
            av_packet_free(&p2);
            break;
        }
    }

    av_frame_free(&vf);
    av_frame_free(&af);
    av_write_trailer(oc);
    avcodec_free_context(&actx);
    avcodec_free_context(&vctx);
    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    avformat_free_context(oc);
    return true;
}

Project make_project(const std::string& src_path) {
    Project p;
    p.name = "LoudnessNormalize";
    p.sequence.fps = 30.0;
    MediaEntry m;
    m.id = 0;
    m.path = src_path;
    m.fps = 30.0;
    m.width = 128;
    m.height = 96;
    m.total_frames = 90;
    p.media.push_back(m);

    Track v;
    v.kind = Track::Kind::Video;
    v.name = "V1";
    p.sequence.video_tracks.push_back(std::move(v));
    Clip vc;
    vc.media = 0;
    vc.name = "V";
    vc.tl_in = 0;
    vc.src_in = 0;
    vc.src_out = 90;
    place_clip(p.sequence, Track::Kind::Video, 0, vc, Placement::Overwrite);

    Track a;
    a.kind = Track::Kind::Audio;
    a.name = "A1";
    p.sequence.audio_tracks.push_back(std::move(a));
    Clip ac;
    ac.media = 0;
    ac.name = "A";
    ac.tl_in = 0;
    ac.src_in = 0;
    ac.src_out = 90;
    place_clip(p.sequence, Track::Kind::Audio, 0, ac, Placement::Overwrite);
    return p;
}

bool read_audio(const std::string& path, std::vector<float>* out, int* channels, int* rate) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
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

    *channels = ctx->ch_layout.nb_channels;
    *rate = ctx->sample_rate;
    out->clear();

    AVFrame* f = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    const auto drain = [&](AVCodecContext* c) {
        while (avcodec_receive_frame(c, f) == 0) {
            const int ch = f->ch_layout.nb_channels;
            const int n = f->nb_samples;
            if (ch <= 0 || n <= 0) continue;
            if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(f->format))) {
                for (int i = 0; i < n; ++i)
                    for (int c2 = 0; c2 < ch; ++c2)
                        out->push_back(reinterpret_cast<float*>(f->extended_data[c2])[i]);
            } else {
                const int bps = av_get_bytes_per_sample(static_cast<AVSampleFormat>(f->format));
                if (f->format == AV_SAMPLE_FMT_FLT)
                    for (int i = 0; i < n * ch; ++i)
                        out->push_back(reinterpret_cast<float*>(f->data[0])[i]);
                else if (f->format == AV_SAMPLE_FMT_S16 && bps == 2)
                    for (int i = 0; i < n * ch; ++i)
                        out->push_back(
                            (float)reinterpret_cast<int16_t*>(f->data[0])[i] / 32768.0f);
            }
            av_frame_unref(f);
        }
    };
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) drain(ctx);
        }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(ctx, nullptr);
    drain(ctx);

    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return !out->empty();
}

float measure(const std::string& path) {
    std::vector<float> pcm;
    int ch = 0;
    int rate = 0;
    if (!read_audio(path, &pcm, &ch, &rate)) return loudness::kSilenceLufs;
    return loudness::integrated_loudness_lufs_interleaved(pcm, ch, rate);
}

ExportSettings base_settings(const std::string& out) {
    ExportSettings s;
    s.output_path = out;
    s.format = "matroska";
    s.video_codec = "ffv1";
    s.audio_codec = "aac";
    s.width = 128;
    s.height = 96;
    s.fps = 30.0;
    s.duration_frames = 90;
    s.audio_sample_rate = 48000;
    s.audio_channels = 2;
    s.audio_bitrate_kbps = 192;
    s.video_bitrate_kbps = 0;
    s.crf = -1;
    s.preset.clear();
    s.extra.clear();
    s.threads = 2;
    return s;
}

}

int main() {
    std::filesystem::remove_all(kOutDir);
    std::filesystem::create_directories(kOutDir);

    const std::string src = std::string(kOutDir) + "/source.mkv";
    if (!make_source(src, 128, 96, 30, 90, 48000)) {
        std::printf("SKIP  no ffv1/pcm_s16le encoders to synthesize source\n");
        return 2;
    }
    const Project p = make_project(src);

    std::string error;
    ExportSettings plain = base_settings(std::string(kOutDir) + "/plain.mkv");
    const bool ok_plain = export_project(p, plain, nullptr, &error);
    check(ok_plain, "un-normalized export succeeds");
    if (!ok_plain) std::printf("      error: %s\n", error.c_str());
    const float plain_lu = measure(plain.output_path);
    check_near(plain_lu, (float)kSrcLufs, 1.0f, "raw export measures the source loudness");

    ExportSettings norm = base_settings(std::string(kOutDir) + "/norm.mkv");
    norm.normalize_loudness = true;
    norm.normalize_target_lufs = kTargetLufs;
    const bool ok_norm = export_project(p, norm, nullptr, &error);
    check(ok_norm, "normalized export succeeds");
    if (!ok_norm) std::printf("      error: %s\n", error.c_str());
    const float norm_lu = measure(norm.output_path);
    check_near(norm_lu, kTargetLufs, 1.0f, "normalized export lands on the target");

    if (ok_plain && ok_norm)
        check_near(norm_lu - plain_lu, kTargetLufs - (float)kSrcLufs, 0.7f,
                   "normalization moved the mix by the expected gain");

    if (failures == 0) {
        std::printf("ALL LOUDNESS NORMALIZE TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
