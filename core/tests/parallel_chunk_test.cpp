#include "cuda_test_common.hpp"

#include "canvas/core/export/parallel_chunk.hpp"
#include "canvas/core/export/render_queue.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++g_failures;
}

}

int main() {
    using namespace cuda_test;
    const std::string clip_path = default_clip_path();

    if (!cuda_runtime_ok()) {
        std::printf("SKIP CUDA runtime not available\n");
        return 2;
    }
    const std::string dev = pick_cuda_encode_device();
    if (dev.empty()) {
        std::printf("SKIP no working CUDA/NVENC encode device\n");
        return 2;
    }
    canvas::core::HwDeviceManager::set_preferred_gpu("cuda", dev);

    const TestClip clip = probe_clip(clip_path);
    if (!clip.valid()) {
        std::printf("SKIP source clip not found/probable: %s\n", clip_path.c_str());
        return 2;
    }

    constexpr int64_t kDefaultFrames = 120;
    int64_t kFrames = kDefaultFrames;
    if (const char* env = std::getenv("CANVAS_CHUNK_TEST_FRAMES")) {
        const long v = std::atol(env);
        if (v >= 60) kFrames = v;
    }

    const auto t_single0 = std::chrono::steady_clock::now();
    const EncResult single = run_export(make_single_clip_project(clip, kFrames), "hevc_nvenc",
                                        "mp4", kTargetWidth, kTargetHeight, kTargetFps,
                                        kFrames, -1, "vbr_target", "medium",
                                        kTargetBitrateKbps, "", "chunk_single");
    const double single_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - t_single0)
                                 .count();
    check(single.ok, "single full-range export succeeds");

    canvas::core::ExportSettings base;
    base.output_path = art_root() + "/out_chunk_cat.mp4";
    base.format = "mp4";
    base.video_codec = "hevc_nvenc";
    base.audio_codec = "";
    base.width = kTargetWidth;
    base.height = kTargetHeight;
    base.fps = kTargetFps;
    base.duration_frames = kFrames;
    base.crf = -1;
    base.vid_rc_mode = "vbr_target";
    base.preset = "medium";
    base.video_bitrate_kbps = kTargetBitrateKbps;
    base.video_max_bitrate_kbps = kTargetBitrateKbps * 3 / 2;

    std::string err;
    const auto t_par0 = std::chrono::steady_clock::now();
    const bool par_ok = canvas::core::export_parallel_chunks(
        make_single_clip_project(clip, kFrames), base, 2, nullptr, &err);
    const double par_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t_par0)
                              .count();
    check(par_ok, ("2-chunk parallel export succeeds: " + err).c_str());

    int cat_frames = 0;
    if (par_ok) {
        const DecodeResult dec = verify_decode(base.output_path, 0.0);
        cat_frames = dec.frames;
        check(dec.ok, "joined output decodes");
        check(dec.frames == kFrames, "joined output carries the full frame count");
        check(dec.lit_luma > 0, "joined output carries non-black content");
    }

    std::printf("chunk timing: single=%.0fms parallel=%.0fms speedup=%.2fx frames=%d\n",
                single_ms, par_ms, par_ms > 0.0 ? single_ms / par_ms : 0.0, cat_frames);

    canvas::core::ExportControl rec_ctrl;
    std::mutex rec_mu;
    std::vector<double> rec_vals;
    rec_ctrl.on_progress = [&](double p, const std::string&) {
        std::lock_guard<std::mutex> lk(rec_mu);
        rec_vals.push_back(p);
    };
    canvas::core::ExportSettings rec_base = base;
    rec_base.output_path = art_root() + "/out_chunk_mon.mp4";
    std::string rec_err;
    const bool rec_ok = canvas::core::export_parallel_chunks(
        make_single_clip_project(clip, kFrames), rec_base, 2, &rec_ctrl, &rec_err);
    check(rec_ok, ("parallel export with recording control succeeds: " + rec_err).c_str());
    {
        bool mono = true;
        for (size_t i = 1; i < rec_vals.size(); ++i) {
            if (rec_vals[i] < rec_vals[i - 1]) {
                mono = false;
                break;
            }
        }
        check(mono && !rec_vals.empty(), "parallel progress never runs backward");
    }

    canvas::core::DeliverSettings ds;
    ds.file.file_name = "out_queue_cat";
    ds.video.format = "MP4";
    ds.video.codec = "H.265";
    ds.video.encoder = canvas::core::EncoderBackend::NVIDIA;
    ds.video.custom_width = kTargetWidth;
    ds.video.custom_height = kTargetHeight;
    ds.video.frame_rate = "Custom";
    ds.video.custom_fps = kTargetFps;
    ds.video.preset = "Medium";
    ds.video.rate_control = canvas::core::RateControl::VBRTargetKbps;
    ds.video.target_bitrate_kbps = kTargetBitrateKbps;
    ds.video.max_bitrate_kbps = kTargetBitrateKbps * 3 / 2;
    ds.video.parallel_chunks = 2;
    ds.audio.export_audio = false;

    canvas::core::RenderJob job;
    job.name = "queue-chunks";
    job.settings = ds;
    job.output_path = art_root() + "/out_queue_cat.mp4";
    job.total_frames = kFrames;

    canvas::core::RenderQueue queue;
    queue.set_active_project(
        std::make_shared<const canvas::core::Project>(make_single_clip_project(clip, kFrames)),
        {});
    queue.enqueue(std::move(job));
    queue.start();
    bool done = false;
    bool queue_ok = false;
    for (int i = 0; i < 600; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (const auto& j : queue.jobs()) {
            if (j.status == canvas::core::RenderJob::Status::Completed) {
                done = true;
                queue_ok = true;
            } else if (j.status == canvas::core::RenderJob::Status::Failed ||
                       j.status == canvas::core::RenderJob::Status::Cancelled) {
                done = true;
                queue_ok = false;
            }
        }
        if (done) break;
    }
    check(done && queue_ok, "render queue completes a 2-chunk job");
    if (done && queue_ok) {
        const DecodeResult qdec = verify_decode(art_root() + "/out_queue_cat.mp4", 0.0);
        check(qdec.ok && qdec.frames == kFrames && qdec.lit_luma > 0,
              "queue chunk job output decodes with the full frame count");
    }

    if (g_failures == 0) {
        std::printf("\nPASS\n");
        return 0;
    }
    std::printf("\nFAIL (%d)\n", g_failures);
    return 1;
}
