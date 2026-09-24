#include "canvas/core/export/parallel_chunk.hpp"

#include "canvas/core/util/log.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace canvas::core {

namespace {

constexpr int64_t kMinChunkFrames = 30;

void remove_quiet(const std::string& path) {
    if (!path.empty()) std::remove(path.c_str());
}

struct ChunkOutcome {
    bool ok = false;
    std::string err;
};

}

bool concat_chunk_files(const std::vector<std::string>& inputs, const std::string& output,
                        const std::string& format, std::string* error) {
    const auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    if (inputs.empty()) return fail("Nothing to concatenate.");
    if (output.empty()) return fail("Concat output path is missing.");

    const AVOutputFormat* out_fmt =
        av_guess_format(format.empty() ? nullptr : format.c_str(), output.c_str(), nullptr);
    if (!out_fmt) out_fmt = av_guess_format(nullptr, output.c_str(), nullptr);
    if (!out_fmt) return fail("Unknown output container for concat.");

    AVFormatContext* oc = nullptr;
    if (avformat_alloc_output_context2(&oc, out_fmt, nullptr, output.c_str()) < 0 || !oc)
        return fail("Failed to allocate concat output context.");

    struct Input {
        AVFormatContext* ctx = nullptr;
        int video_stream = -1;
    };
    std::vector<Input> opened;
    opened.reserve(inputs.size());
    AVStream* out_vs = nullptr;
    AVRational out_tb{1, 90000};

    for (const std::string& path : inputs) {
        AVFormatContext* ic = nullptr;
        if (avformat_open_input(&ic, path.c_str(), nullptr, nullptr) < 0 || !ic) {
            for (auto& o : opened)
                if (o.ctx) avformat_close_input(&o.ctx);
            avformat_free_context(oc);
            return fail("Concat cannot open chunk: " + path);
        }
        if (avformat_find_stream_info(ic, nullptr) < 0) {
            avformat_close_input(&ic);
            for (auto& o : opened)
                if (o.ctx) avformat_close_input(&o.ctx);
            avformat_free_context(oc);
            return fail("Concat cannot probe chunk: " + path);
        }
        int best = -1;
        int videos = 0;
        for (unsigned i = 0; i < ic->nb_streams; ++i) {
            const AVStream* st = ic->streams[i];
            if (!st || !st->codecpar) continue;
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                avformat_close_input(&ic);
                for (auto& o : opened)
                    if (o.ctx) avformat_close_input(&o.ctx);
                avformat_free_context(oc);
                return fail("Chunked export joined a chunk with audio; audio is not supported.");
            }
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                ++videos;
                best = (int)i;
            }
        }
        if (videos != 1 || best < 0) {
            avformat_close_input(&ic);
            for (auto& o : opened)
                if (o.ctx) avformat_close_input(&o.ctx);
            avformat_free_context(oc);
            return fail("Concat needs exactly one video stream per chunk: " + path);
        }
        if (!out_vs) {
            out_vs = avformat_new_stream(oc, nullptr);
            if (!out_vs) {
                avformat_close_input(&ic);
                for (auto& o : opened)
                    if (o.ctx) avformat_close_input(&o.ctx);
                avformat_free_context(oc);
                return fail("Concat cannot create output stream.");
            }
            if (avcodec_parameters_copy(out_vs->codecpar,
                                        ic->streams[best]->codecpar) < 0) {
                avformat_close_input(&ic);
                for (auto& o : opened)
                    if (o.ctx) avformat_close_input(&o.ctx);
                avformat_free_context(oc);
                return fail("Concat cannot copy chunk codec parameters.");
            }
            out_vs->time_base = ic->streams[best]->time_base;
            out_tb = out_vs->time_base;
        } else {
            const AVCodecParameters* a = out_vs->codecpar;
            const AVCodecParameters* b = ic->streams[best]->codecpar;
            if (!a || !b || a->codec_id != b->codec_id || a->width != b->width ||
                a->height != b->height ||
                (a->extradata_size != b->extradata_size ||
                 (a->extradata_size > 0 && a->extradata && b->extradata &&
                  std::memcmp(a->extradata, b->extradata,
                              (size_t)a->extradata_size) != 0))) {
                avformat_close_input(&ic);
                for (auto& o : opened)
                    if (o.ctx) avformat_close_input(&o.ctx);
                avformat_free_context(oc);
                return fail("Chunk codec parameters differ; cannot join: " + path);
            }
        }
        opened.push_back({ic, best});
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, output.c_str(), AVIO_FLAG_WRITE) < 0) {
            for (auto& o : opened)
                if (o.ctx) avformat_close_input(&o.ctx);
            avformat_free_context(oc);
            return fail("Concat cannot open output file.");
        }
    }
    if (avformat_write_header(oc, nullptr) < 0) {
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        for (auto& o : opened)
            if (o.ctx) avformat_close_input(&o.ctx);
        avformat_free_context(oc);
        return fail("Concat cannot write output header.");
    }

    int64_t offset = 0;
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        for (auto& o : opened)
            if (o.ctx) avformat_close_input(&o.ctx);
        avformat_free_context(oc);
        return fail("Concat out of memory.");
    }
    for (auto& in : opened) {
        const AVRational in_tb = in.ctx->streams[in.video_stream]->time_base;
        int64_t chunk_end = offset;
        while (av_read_frame(in.ctx, pkt) == 0) {
            if (pkt->stream_index != in.video_stream) {
                av_packet_unref(pkt);
                continue;
            }
            if (pkt->pts != AV_NOPTS_VALUE) {
                pkt->pts = av_rescale_q(pkt->pts, in_tb, out_tb) + offset;
                chunk_end = std::max(chunk_end, pkt->pts);
            }
            if (pkt->dts != AV_NOPTS_VALUE) {
                pkt->dts = av_rescale_q(pkt->dts, in_tb, out_tb) + offset;
                chunk_end = std::max(chunk_end, pkt->dts);
            }
            if (pkt->duration > 0)
                chunk_end = std::max(chunk_end, pkt->dts + av_rescale_q(pkt->duration, in_tb, out_tb));
            pkt->stream_index = out_vs->index;
            if (av_interleaved_write_frame(oc, pkt) < 0) {
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
                for (auto& o : opened)
                    if (o.ctx) avformat_close_input(&o.ctx);
                avformat_free_context(oc);
                return fail("Concat failed while joining chunks.");
            }
        }
        offset = chunk_end;
    }
    av_packet_free(&pkt);
    av_write_trailer(oc);
    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    for (auto& o : opened)
        if (o.ctx) avformat_close_input(&o.ctx);
    avformat_free_context(oc);
    return true;
}

bool export_parallel_chunks(const Project& project, const ExportSettings& base, int chunks,
                            ExportControl* control, std::string* error) {
    const auto fail = [&](const std::string& m) {
        ::canvas::core::log::log_error("parallel export failure: %s", m.c_str());
        if (error) *error = m;
        return false;
    };
    int want = chunks < 2 ? 2 : chunks;
    if (want > 4) want = 4;
    if (base.duration_frames < want * kMinChunkFrames)
        return export_project(project, base, control, error);
    if (!base.audio_codec.empty())
        return fail("Parallel chunks do not support audio yet; disable audio or use single export.");
    if (!base.chapters.empty())
        return fail("Parallel chunks do not support embedded chapters yet.");

    const int64_t total = base.duration_frames;
    const int64_t base_start = base.start_frame > 0 ? base.start_frame : 0;

    std::vector<ExportSettings> parts;
    std::vector<std::string> paths;
    parts.reserve((size_t)want);
    paths.reserve((size_t)want);
    int64_t cursor = 0;
    for (int i = 0; i < want; ++i) {
        const int64_t len = (total - cursor) / (want - i);
        ExportSettings es = base;
        es.start_frame = base_start + cursor;
        es.duration_frames = len;
        es.output_path = base.output_path + ".chunk" + std::to_string(i);
        es.chapters.clear();
        parts.push_back(es);
        paths.push_back(es.output_path);
        cursor += len;
    }

    std::mutex mu;
    std::vector<ChunkOutcome> outcomes((size_t)want);
    std::atomic<double> sent{0.0};
    std::vector<std::thread> workers;
    workers.reserve((size_t)want);
    for (int i = 0; i < want; ++i) {
        workers.emplace_back([&, i] {
            ExportControl chunk_ctrl;
            if (control && control->should_cancel)
                chunk_ctrl.should_cancel = control->should_cancel;
            const int64_t lo = parts[(size_t)i].start_frame - base_start;
            const int64_t ln = parts[(size_t)i].duration_frames;
            if (control) {
                chunk_ctrl.on_progress = [&, lo, ln](double p, const std::string&) {
                    const double g = total > 0
                                         ? (double)(lo + (int64_t)(p * (double)ln)) / (double)total
                                         : 0.0;
                    const double c = std::clamp(g, 0.0, 1.0);
                    double prev = sent.load();
                    while (c > prev && !sent.compare_exchange_weak(prev, c)) {
                    }
                    if (c > prev) control->on_progress(c, "Encode");
                };
                if (i == 0) chunk_ctrl.on_frame = control->on_frame;
            }
            ChunkOutcome oc;
            oc.ok = export_project(project, parts[(size_t)i], control ? &chunk_ctrl : nullptr,
                                   &oc.err);
            std::lock_guard<std::mutex> lk(mu);
            outcomes[(size_t)i] = std::move(oc);
        });
    }
    for (auto& t : workers)
        if (t.joinable()) t.join();

    for (const auto& oc : outcomes) {
        if (!oc.ok) {
            for (const auto& p : paths) remove_quiet(p);
            return fail(oc.err.empty() ? "A chunk export failed." : oc.err);
        }
    }
    if (control) control->on_progress(1.0, "Concat");
    if (!concat_chunk_files(paths, base.output_path, base.format, error)) {
        for (const auto& p : paths) remove_quiet(p);
        const std::string msg = (error && !error->empty()) ? *error : "Chunk concat failed.";
        if (error) *error = msg;
        ::canvas::core::log::log_error("parallel export failure: %s", msg.c_str());
        return false;
    }
    for (const auto& p : paths) remove_quiet(p);
    if (control) control->on_progress(1.0, "Encode");
    return true;
}

}
