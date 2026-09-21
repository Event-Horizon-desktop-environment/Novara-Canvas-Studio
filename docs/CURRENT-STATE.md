# Current State

Honest status as of 2026-09-17. This is an **alpha-grade** nonlinear video
editor. A lot works, some things are rough, and a few known issues are parked
with clear explanations.

## At a glance

- **Name / build identity:** Novara Canvas Studio, binary `canvas`. Renamed
  from "Event Horizon Studio" on 2026-09-05 as Nova Canvas Studio, and to
  **Novara** on 2026-09-21 — namespace `canvas::`, include dir
  `core/include/canvas/`, lib `canvas_core`, project file key
  `canvas_project` (legacy `event_horizon_project` still loads).
- **Version:** 0.1.0, C++20, Linux only.
- **Builds:** clean (zero warnings) in Debug and Release into `./build`
  (and `build-release/`). `-Werror` is PUBLIC on `canvas_core`
  (`-Wall -Wextra -Wpedantic`), so every consumer — the GUI app, all core
  tests, all headless/Qt/Vulkan tests — inherits it.
- **Tests:** 77 in CTest (`ctest --test-dir build`) — 47 core + 14
  GUI-headless + 6 GUI Qt-linked (offscreen) + 10 Vulkan. 75 pass; the two
  non-passing tests are SKIP-as-fail WIP stubs (`scrub_bench_vulkan_test`,
  `visual_render_parity_test`) owned by the unfinished Vulkan render kernel —
  not regressions.

## What works today

**The editor core**
- Undoable editing: place/unlink/link/lift/ripple-delete/blade/move/transition/
  delete-through-edit, via a snapshot-based command stack
- Linked A/V clip pairs move, delete, and transition together
- Per-clip speed change (0.1×–10×) with pitch-preserving WSOLA time-stretch,
  and pitch shift (±12 semitones / ±100 cents) — one law shared by playback,
  export, and the Inspector
- Timeline model (de)serialization to `.ncs` (JSON), versioned; legacy
  `.ehproj` still opens
- Markers/bookmarks (point + range), 3-point editing law, track add/remove/
  collapse ops, clip blend modes — all headless-layered and regression-tested
- Autosave (`.autosave-` turnover slots), action registry + Ctrl+K-style
  action search, background render-queue policy — all headless-layered

**The timeline UI**
- Dark, tool-grade UI with pinned top strip: live timecode readout,
  minimap, ruler with tick labels
- Playhead drawn over the ruler/grid; ruler-click seek and playhead drag
- Divider band between the channel groups: grab it to pan the channels —
  pull **up** clamps at the limit (the channels' resting seat under the
  ruler), pull **down** sinks them through a deep scroll room
- Clip drag with snap, zoom-dependent quantization, selection model with
  linked-mate coalescing, V1/A1 resize edges on the band
- Filmstrip thumbnails + waveform previews (async thumbnail service)
- Per-clip volume line (drag directly on the audio clip), 12-swatch
  Resolve-style clip colours, drag ghost + track-stack drop preview
- The pure interaction math (snap, selection, drag sessions, volume-line
  laws, transition-handle editing) has been extracted into **Qt-free headless
  modules** under `gui/src/Widgets/` and `gui/src/features/`, each with its
  own CTest target

**Media & playback**
- FFmpeg decode with shared hardware decode device (cuda → vaapi → qsv →
  vulkan, falling back to software), frame cache, low-res preview cap
- Command-worker playback stack with 24-frame lookahead, scrub preview
  cache, embedded transitions, ALSA/PipeWire audio output, A/V sync logging
- `TimelineDecoder`, `AudioPipeline`, and `SonicSync` are now headless
  modules with a frozen public seam; audio reaches the device only through
  an abstract `AudioSink` (fake-sink-tested)

**Export**
- Deliver-panel style settings, NLE-style codec/container lists
- NVENC/VAAPI/QSV hardware encoders with CPU fallback, cancel + progress
- Background render queue; GPU NV12 fast path when CUDA is available
- Per-clip speed/pitch/volume/pan/fade and transform/composite/title blends
  honoured in export; EDL export (CMX3600) and chapters-from-markers muxing
  on MP4/WebM/MKV-family outputs

**Testing infrastructure**
- Headless Qt-free harness: core engine + extracted GUI logic compile and run
  with no Qt and no display (enforced at build time by linkage + a static
  `scripts/check_qtdep.sh` guard)

## Recent changes

- **Full rename to Novara Canvas Studio** (2026-09-21) — display-name
  rebrand from Nova Canvas Studio; binary `canvas`, namespace, and project
  file key unchanged.
- **Full rename to Nova Canvas Studio** (2026-09-05) — code, CMake,
  packaging, app identity. Legacy `.ehproj` files still open.
- **Headless extraction (the original 40-phase split, stages 1–30)** — the playback stack
  (`timeline_decoder`, `audio_pipeline`, `sonicsync`, `sync_constants`,
  `audio_sink`) and the timeline interaction suite (snap, selection, drag,
  volume line, transition handle editor, audio targets, deliver settings
  model, source preview model) are Qt-free modules with their own tests.
- **`-Werror` hardening** — PUBLIC on `canvas_core` and inherited by every
  target (GNU/Clang); the tree builds warning-free in Debug, Release, and
  `build-release/`.
- **`roundtrip` test fixed** — a pre-existing SEGFAULT on a stale timeline
  model was closed (edge now passes) and the test is green again.
- **Vulkan-tests family grown to 10** — probe/queue/profile/format/interop/
  vram/export-sweep tests run where a Vulkan device exists.
- **Planning-doc cleanup (2026-09-17)** — the old planning/design notes were
  removed from the repo; see *Where the docs diverge from reality* below.

## Known issues & quirks

- **Two Vulkan render tests are SKIP stubs** — `scrub_bench_vulkan_test` and
  `visual_render_parity_test` are owned by the unfinished Vulkan render
  kernel (Vulkan phases P-C/P-D). Treat them as disabled, not as regressions.
- **Two decoders overlap** — `VideoDecoder::decode_audio` vs the standalone
  `AudioDecoder`. Playback uses the standalone one; de-duplication is
  planned.
- **Stale-audio flush quirk** — `AudioOutput::flush()` must join the ALSA
  writer thread before being dropped; this is documented in the code and
  worked around at call sites.
- **Transition-handle preset snapping is a no-op** — a documented latent bug
  in the drag-session editor, locked by a test; the one-line fix is
  `best = INT64_MAX`.
- **`I` key collision** — `I` is bound to both Mark In and the Inspector
  toggle (documented collision).
- **Export-wiring gaps in newer Deliver fields** — audio loudness
  normalization (target LUFS), data burn-in / caption burn, and
  still/frame-sequence render scopes have UI + headless laws + regression
  tests and persist in project files, but are not yet applied by the
  mux/encode path (open wiring).
- **GPU is a build-time bonus, not a requirement.** Without CUDA you get CPU
  encoding; without ALSA/PipeWire you get no audio but still full video.
- **Automated GUI coverage is offscreen-widget-only** — 6 Qt-linked tests run
  under `QT_QPA_PLATFORM=offscreen` (wheel panel, theme, volume-line drag,
  waveform placement, thumbnail namespace/disk serve); the full
  mouse-driven viewer/timeline interaction layer is still verified by
  launching the real app.

## Where the docs diverge from reality

- The pre-rename planning notes — root `ux.md`, `n.md`, `projectmap.md`,
  `a.plan.md`, `splitplan.md`, `features.md`, and the stale `%08Video-Editor*`
  duplicate checkout — **no longer exist**. They were removed in the
  2026-09-17 cleanup, and the planning content now lives in a private
  project map **outside the repo**; any surviving references to them are
  historical.
- On **2026-09-17** the in-repo docs were re-audited against the source:
  `README.md`, `roadmap.md`, `AGENTS.md`, and everything under `docs/`
  (`ARCHITECTURE`, `BUILDING`, `DEPENDENCIES`, `FEATURES`, `KEYBINDS`,
  `inspector`, `nvenc`, `vaapi`, `video`, `vulkan`, `node-system-research`,
  and the active phase log `PHASE1.md`).
- Counts still go stale fast — always re-check with
  `ctest --test-dir build -N` before quoting a test count or pass status.

## Sensible next steps

- Land the Vulkan render kernel (unlocks the two SKIP-stub tests)
- Unify the two audio decode paths
- Finish the remaining split-plan work (Deliver-panel split, dead-code
  removal, timeline-widget cleanup)
- Wire the still-in-flight Deliver fields (loudness normalization, caption
  burn, still/frame-sequence scopes) into the export path
- Keep the extraction-then-test pattern for GUI logic and the zero-warning +
  no-Qt-in-core discipline on every change