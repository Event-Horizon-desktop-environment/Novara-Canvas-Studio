# Parallel-Chunk Export — Design

Date: 2026-09-23. Status: prototype measured (video-only, 2 chunks).

## Prototype result

`parallel_chunk` test (`core/tests/parallel_chunk_test.cpp`): two threads
export 60-frame halves in-process (shared export HW cache), joined with
the in-process stream-copy concat. 120/120 frames, non-black, single
535 ms vs parallel 326 ms = 1.64x. A second phase drives the real queue
path (`DeliverSettings` with `parallel_chunks=2` through `RenderQueue`)
and verifies the joined file too.

## Product wiring (landed)

- `DeliverVideoSettings.parallel_chunks` (1/2, default 1), serialized
  with the project, mapped through `to_export_settings`.
- Deliver panel "Parallel Chunks" combo (Off/2 chunks) in Quality &
  Bitrate; auto-disabled unless whole-timeline scope with audio off.
- `RenderQueue::run_job` fans out to `export_parallel_chunks` when
  eligible (video-only, no chapters, >= 60 frames), else single-pipe.

## Goal

Cut wall-clock export time with N parallel NVENC sessions over timeline
segments, concatenated losslessly. Only remaining >1.5x path: single-pipe
export is at ~85% of its practical ceiling (dav1d decode ~0.8 ms/frame +
NVENC submit fence ~1 ms/frame + kernels 75 us/frame).

## Measured ceiling (RTX 5070 Ti, 1440p60 AV1 source, hevc_nvenc p4 VBR)

600-frame workload, separate ffmpeg processes, decode threads capped:

| chunks | wall | speedup |
|---|---|---|
| 1 | 2.16 s | 1.00x |
| 2 | 1.49 s | 1.45x |
| 4 | 1.74 s | 1.24x |

4x regresses: dav1d starves at 3 threads, 4x startup/seek cost, NVENC
contention. dav1d beats NVDEC on wall (1197 vs 937 fps), so chunks keep
software decode with a per-chunk thread cap, not NVDEC.

## Design

- `ExportSettings.start_frame` (additive, default 0): first timeline frame
  of the segment. `duration_frames` is the segment length. Output
  timestamps stay chunk-local from 0; the concat step re-bases them.
- Fan-out: split `[0, duration)` into N contiguous ranges, run
  `export_project` per range on N threads (each with its own FramePump,
  RenderSession, encoder instance; the export HW device cache is shared
  and mutex-guarded).
- Concat: identical codec params across chunks, every chunk starts with
  an IDR (fresh encoder instance always emits one), join with the ffmpeg
  concat demuxer + stream copy. No re-encode.
- Progress/cancel: fan the single ExportControl into per-chunk controls
  mapping local progress to the global range; any chunk failure or cancel
  aborts the whole export and removes partials.
- Audio: prototype is video-only. AAC priming (~1024 samples) leaves a
  seam at every joint; chunked audio needs priming compensation or
  re-encode of boundary GOPs. Explicit non-goal for v1.
- Default: off. Deliver toggle (off/2). Expected gain ~1.3-1.5x on heavy
  (AV1/HEVC 1440p+) content, up to ~2x on light (H.264) content.
  Measured in-process: 1.64x at 120 frames, 1.71x at 600 frames
  (sustained, no falloff).
- When single-pipe already saturates NVENC (fast presets, light
  content), chunks split the same engine and can run slower than single.
  Rule: use chunks when single export is feeder-bound (well under
  ~800 fps at 1440p); leave it off otherwise.

## Risks

- Boundary rate-control resets cost a few % bitrate efficiency; more
  chunks = more overhead. Cap at 2 until data says otherwise.
- GeForce concurrent NVENC sessions are driver-limited (historically ~5);
  2 is safe territory, verified live with parallel sessions active.
- Concat requires byte-compatible codecpar; any per-chunk settings
  divergence breaks the join. Fan-out must share one settings object.
- Seams are invisible for video (IDR boundaries) but audible for audio
  (see above); never enable chunking with audio until compensated.

## Prototype plan

1. `start_frame` field + honoring in `export_project` (this doc's change).
2. Headless 2-chunk test: two threads, concat via ffmpeg CLI, wall vs
   single + frame-count + boundary-decode verification.
3. RenderQueue fan-out + Deliver toggle (needs GUI sign-off on defaults).
