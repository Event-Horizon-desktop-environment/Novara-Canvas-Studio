#include "canvas/core/export/deliver_preset.hpp"
#include "canvas/core/export/exporter.hpp"
#include "canvas/core/project/project.hpp"
#include "canvas/core/timeline/edit_ops.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
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

void check(bool cond, const char* what) {
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

const char* kOutDir = "/tmp/canvas_audio_codecs";

bool make_source(const std::string& path) {
    const int w = 128, h = 96, fps = 30, frames = 30, sample_rate = 48000;
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
    if (avcodec_open2(vctx, vcodec, nullptr) < 0 ||
        avcodec_parameters_from_context(vst->codecpar, vctx) < 0) {
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
    const int64_t total_samples = (int64_t)frames * sample_rate / fps;
    int64_t cursor = 0;
    for (int n = 0; n < frames; ++n) {
        av_frame_make_writable(vf);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                vf->data[0][y * vf->linesize[0] + x] = (uint8_t)((x + y + n) & 0xff);
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
        while (cursor < total_samples) {
            av_frame_make_writable(af);
            const int want = (int)std::min<int64_t>(1024, total_samples - cursor);
            auto* d = reinterpret_cast<int16_t*>(af->data[0]);
            for (int i = 0; i < want; ++i) {
                const double t = (double)(cursor + i) / (double)sample_rate;
                const float v = (float)(0.25 * std::sin(2.0 * std::numbers::pi * 440.0 * t));
                const int16_t s = (int16_t)(v * 32767.0f);
                d[i * 2] = s;
                d[i * 2 + 1] = s;
            }
            for (int i = want; i < 1024; ++i) {
                d[i * 2] = 0;
                d[i * 2 + 1] = 0;
            }
            af->nb_samples = 1024;
            af->pts = cursor;
            cursor += want;
            avcodec_send_frame(actx, af);
            AVPacket* ap = av_packet_alloc();
            while (avcodec_receive_packet(actx, ap) == 0) {
                av_packet_rescale_ts(ap, actx->time_base, ast->time_base);
                ap->stream_index = ast->index;
                av_interleaved_write_frame(oc, ap);
                av_packet_unref(ap);
            }
            av_packet_free(&ap);
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

Project make_project(const std::string& src) {
    Project p;
    p.name = "AudioCodecs";
    p.active_sequence().fps = 30.0;
    MediaEntry m;
    m.id = 0;
    m.path = src;
    m.fps = 30.0;
    m.width = 128;
    m.height = 96;
    m.total_frames = 30;
    p.media.push_back(m);
    Track v;
    v.kind = Track::Kind::Video;
    v.name = "V1";
    p.active_sequence().video_tracks.push_back(std::move(v));
    Clip vc;
    vc.media = 0;
    vc.tl_in = 0;
    vc.src_in = 0;
    vc.src_out = 30;
    place_clip(p.active_sequence(), Track::Kind::Video, 0, vc, Placement::Overwrite);
    Track a;
    a.kind = Track::Kind::Audio;
    a.name = "A1";
    p.active_sequence().audio_tracks.push_back(std::move(a));
    Clip ac;
    ac.media = 0;
    ac.tl_in = 0;
    ac.src_in = 0;
    ac.src_out = 30;
    place_clip(p.active_sequence(), Track::Kind::Audio, 0, ac, Placement::Overwrite);
    return p;
}

bool decode_peak(const std::string& path, float* peak) {
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
    float pk = 0.0f;
    AVFrame* f = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    const auto drain = [&]() {
        while (avcodec_receive_frame(ctx, f) == 0) {
            const int ch = f->ch_layout.nb_channels;
            const int n = f->nb_samples;
            const auto fmtid = static_cast<AVSampleFormat>(f->format);
            if (fmtid == AV_SAMPLE_FMT_FLT || fmtid == AV_SAMPLE_FMT_FLTP) {
                if (av_sample_fmt_is_planar(fmtid)) {
                    for (int c = 0; c < ch; ++c) {
                        const auto* d = reinterpret_cast<float*>(f->extended_data[c]);
                        for (int i = 0; i < n; ++i) {
                            const float v = std::fabs(d[i]);
                            if (v > pk) pk = v;
                        }
                    }
                } else {
                    const auto* d = reinterpret_cast<float*>(f->data[0]);
                    for (int i = 0; i < n * ch; ++i) {
                        const float v = std::fabs(d[i]);
                        if (v > pk) pk = v;
                    }
                }
            } else if (fmtid == AV_SAMPLE_FMT_S16 || fmtid == AV_SAMPLE_FMT_S16P) {
                if (av_sample_fmt_is_planar(fmtid)) {
                    for (int c = 0; c < ch; ++c) {
                        const auto* d = reinterpret_cast<int16_t*>(f->extended_data[c]);
                        for (int i = 0; i < n; ++i) {
                            const float v = std::fabs((float)d[i] / 32768.0f);
                            if (v > pk) pk = v;
                        }
                    }
                } else {
                    const auto* d = reinterpret_cast<int16_t*>(f->data[0]);
                    for (int i = 0; i < n * ch; ++i) {
                        const float v = std::fabs((float)d[i] / 32768.0f);
                        if (v > pk) pk = v;
                    }
                }
            } else if (fmtid == AV_SAMPLE_FMT_S32 || fmtid == AV_SAMPLE_FMT_S32P) {
                if (av_sample_fmt_is_planar(fmtid)) {
                    for (int c = 0; c < ch; ++c) {
                        const auto* d = reinterpret_cast<int32_t*>(f->extended_data[c]);
                        for (int i = 0; i < n; ++i) {
                            const float v = std::fabs((float)((double)d[i] / 2147483648.0));
                            if (v > pk) pk = v;
                        }
                    }
                } else {
                    const auto* d = reinterpret_cast<int32_t*>(f->data[0]);
                    for (int i = 0; i < n * ch; ++i) {
                        const float v = std::fabs((float)((double)d[i] / 2147483648.0));
                        if (v > pk) pk = v;
                    }
                }
            }
            av_frame_unref(f);
        }
    };
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx && avcodec_send_packet(ctx, pkt) == 0) drain();
        av_packet_unref(pkt);
    }
    avcodec_send_packet(ctx, nullptr);
    drain();
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    *peak = pk;
    return pk > 0.0f;
}

ExportSettings base_settings(const std::string& out, const std::string& audio_codec) {
    ExportSettings s;
    s.output_path = out;
    s.format = "matroska";
    s.video_codec = "ffv1";
    s.audio_codec = audio_codec;
    s.width = 128;
    s.height = 96;
    s.fps = 30.0;
    s.duration_frames = 30;
    s.audio_sample_rate = 48000;
    s.audio_channels = 2;
    s.audio_bitrate_kbps = 192;
    s.video_bitrate_kbps = 0;
    s.crf = -1;
    s.preset.clear();
    s.extra.clear();
    return s;
}

}  // namespace

int main() {
    std::filesystem::remove_all(kOutDir);
    std::filesystem::create_directories(kOutDir);
    const std::string src = std::string(kOutDir) + "/src.mkv";
    if (!make_source(src)) {
        std::printf("SKIP  no ffv1/pcm_s16le encoders to synthesize source\n");
        return 2;
    }
    const Project p = make_project(src);

    check(audio_encoder_name("PCM") == "pcm_s16le", "map PCM -> pcm_s16le");
    check(audio_encoder_name("MP3") == "libmp3lame", "map MP3 -> libmp3lame");
    check(audio_encoder_name("AAC") == "aac", "map AAC -> aac");
    check(audio_encoder_name("FLAC") == "flac", "map FLAC -> flac");
    check(audio_encoder_name("ALAC") == "alac", "map ALAC -> alac");
    check(audio_encoder_name("AC-3") == "ac3", "map AC-3 -> ac3");
    check(audio_encoder_name("E-AC-3") == "eac3", "map E-AC-3 -> eac3");
    check(audio_encoder_name("Opus") == "opus", "map Opus -> opus");
    check(audio_encoder_name("Vorbis") == "vorbis", "map Vorbis -> vorbis");

    DeliverSettings ds;
    ds.audio.codec = "PCM";
    ds.audio.export_audio = true;
    ds.audio.sample_rate = 48000;
    ds.audio.channels = 2;
    ds.video.custom_width = 128;
    ds.video.custom_height = 96;
    ds.video.custom_fps = 30.0;
    check(to_export_settings(ds).audio_codec == "pcm_s16le",
          "to_export_settings carries pcm_s16le");
    ds.audio.codec = "AC-3";
    check(to_export_settings(ds).audio_codec == "ac3", "to_export_settings carries ac3");

    const std::vector<std::string> codecs{"pcm", "pcm_s16le", "mp3", "aac", "flac", "alac",
                                          "ac3", "eac3", "opus", "vorbis"};
    for (const std::string& ac : codecs) {
        ExportSettings es = base_settings(std::string(kOutDir) + "/out_" + ac + ".mkv", ac);
        std::string err;
        const bool ok = export_project(p, es, nullptr, &err);
        check(ok, ("export audio_codec=" + ac).c_str());
        if (!ok) {
            std::printf("      error: %s\n", err.c_str());
            continue;
        }
        float peak = 0.0f;
        check(decode_peak(es.output_path, &peak) && peak > 0.05f,
              ("tone survives " + ac).c_str());
    }

    if (failures == 0) {
        std::printf("ALL AUDIO CODEC TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
