# Architecture

A deliberately clean split: **a headless engine** (`core/`) that knows
nothing about Qt, and **a Qt GUI shell** (`gui/`) that owns everything to do
with pixels, widgets, and user interaction. The pattern is maintained
religiously — the engine has zero Qt includes, and the most testable parts of
the GUI are extracted into Qt-free modules precisely so they can be verified
headlessly.

```
core/                     canvas_core static lib — editing, media, colour, export
  include/canvas/core/    public headers, mirrors src/ below
  src/                    implementations (+ gpu/cuda_convert.cu)
  third_party/rnnoise/    vendored RNNoise (voice isolation)
  tests/                  core unit/bench tests (see BUILDING.md)
gui/                      Qt 6 application
  src/main.cpp            entry point
  src/UX/                 MainWindow, shell builders, inspectors, theme, style, logging
  src/features/           MainWindow behavior split by domain (app, project, timeline,
                          playback, deliver, thumbnails, source preview, colour)
  src/Widgets/            timeline widget + viewer + media pool + toolbox
  src/core/timecode.hpp   frame → HH:MM:SS:FF
  ui/MainWindow.ui        designer shell (docks only; rest programmatic)
  resources.qrc + resources/icons/   SVG icon set
```

No exceptions in the editing path — errors travel through `std::optional`.
Positions in the timeline are `int64_t` frame numbers; `fps` lives on the
`Sequence`.

## core/ — the engine

### Timeline model

`timeline/model.hpp` is THE data model. `Project { name, Sequence, media[],
bins }`; `Sequence { fps, video_tracks, audio_tracks, bookmarks,
next_clip_id }`; `Track { Kind{Video,Audio}, name, locked, solo, clips }`;
`Clip { media, tl_in/out, src_in/out, linked_id, enabled, transition_out/in,
volume_db, pan, scale_x/y, pos_x/y, rotation_deg, anchor_dx/dy, opacity,
blend_mode }`, plus per-clip pitch/speed, a 6-band EQ, voice isolation,
clip colour/tag/comments, title, captions, and a serialized
`grade_graph::GradeGraph`. Tracks are `std::vector<Track>`, lookups are
linear.

Editing goes through `timeline/edit_ops.hpp` — an undoable edit API where
every operation returns a `std::unique_ptr<ICommand>` and undo works by
snapshotting. Operations include `place_clip`, `place_linked_clip`,
`unlink/link_clip`, `lift_range`, `ripple_delete_range`, `blade_at`,
`move_clip`/`move_clips_batch`, `trim_clip_head/tail`, `set_clip_audio`,
`set_clip_transform`, `set_clip_composite`, `set_clip_grade`,
`set_clip_transition`, `set_clip_title`, the `set_track_*` family
(mute/solo/lock/gain/collapse), and `delete_through_edit`, all backed by
`UndoStack`. Linked A/V pairs move, delete, and transition together. The
shared numeric ranges/laws behind the Inspector live in `visual.hpp`
(transform/composite), `audio_mix.hpp` (volume/pan), `audio_fade.hpp`
(transition ramps), `clip_rate.hpp` (speed/pitch), and `time_stretch.hpp`
(pitch-preserving WSOLA).

### Media pipeline

- `frame.hpp` — runtime containers: `VideoFrame` (CPU RGBA + stride),
  `Nv12Frame`/`RenderFrame` (a/b frames + transition progress), `AudioChunk`
  (float PCM).
- `video_decoder` — FFmpeg decoding. Hardware decode through a shared device,
  CPU RGBA via swscale, a low-res preview cap, and keyframe-seek vs
  sequential-forward heuristics. Opens a *separate* audio demuxer/seek domain.
- `sw_decode` — the shared software demux/decode state machine (keyframe
  index, stall accounting) that `video_decoder` builds on.
- `audio_decoder` — standalone PIMPL audio decoder, persistent, buffered,
  resampled. Note: it overlaps with `VideoDecoder::decode_audio`; a caller
  picks one.
- `frame_cache` — thread-safe, byte-budgeted LRU (~512 MB) of decoded frames.
- `hw_device` — a shared `HwDeviceManager` probing cuda → vaapi → qsv →
  vulkan once, falling back to software.
- `gpu_select` — enumerates GPU devices/vendors and maps them to a decoder
  backend + device argument.
- `audio_waveform` — full-stream scan producing per-bucket peak/RMS; re-buckets
  via `reduce_waveform`.
- `equalizer` — the 6-band parametric EQ law shared by playback, export, and
  the Inspector's response graph.
- `voice_isolation` — RNNoise-backed AI voice isolation (`VoiceIsolationMode`).
- `transcribe`/`transcript` — local whisper.cpp (pinned v1.9.4) transcription
  producing a `Transcript` the caption pipeline consumes.
- `vaapi/` — VAAPI surface/driver/vendor helpers and the shared zero-copy
  export path (`surface`, `driver`, `amd`/`intel`/`nvidia`, `export`).

### Project, export, GPU, colour, util

- `project/` — `Project` JSON save/load, versioned (`kProjectVersion`). Files
  use the `.ncs` extension; the JSON key is `canvas_project` (legacy
  `.ehproj` files and the old `event_horizon_project` key still read fine).
  `autosave` writes interval snapshots under a small retention policy.
- `export/` — `deliver_preset` holds the high-level Deliver settings model
  mapped down to low-level `ExportSettings`; `exporter` runs the FFmpeg
  mux+encode (NVENC/VAAPI/QSV hardware encoders, CPU fallback, progress +
  cancel) and stamps chapters from markers; `renderer` turns timeline → frames
  (top-down compositing applying per-clip volume/pan/transform/blend/opacity, a
  pitch-preserving WSOLA speed stretch, a GPU single-clip fast path returning
  borrowed NV12 planes, `render_audio_chunk` mixing); `render_queue` runs
  exports on a background worker thread through `queue_policy`; `edl` writes
  CMX3600 edit lists, `chapters` derives container chapters, `loudness`
  measures/normalizes program loudness (LUFS), and `grade_frame` bakes a
  `GradeGraph` onto a frame. `vaapi_encode`/`qsv_encode` hold the hardware
  encoder rate-control/preset mapping.
- `colorsci/` — the colour-science primitives shared by the Color page:
  `wheels`, `cdl` (ASC-CDL slope/offset/power/sat), `curves`, `histogram`, and
  `wheels_ui`.
- `grade_graph/` — the serializable node-graph grade model: `graph`, `eval`,
  `op`, `edit`, `composite`, `lut` (3D grade-LUT bake), `serialize`.
- `gpu/` — CUDA kernels feeding NVENC directly: `rgbaToNV12` (CPU RGBA →
  NV12), `nv12Resize` (letterbox resize), the fused `nv12GradeResize` (grade +
  resize in one pass), and `nv12TitleBlend` (title sprite compositing).
  Compiled only when `CANVAS_HAVE_CUDA`; callers guard with
  `cuda_available()`. BT.709 limited range.
- `qc/` — frame-sanity metrics used to verify rendered/exported output.
- `actions/` — the core action registry (`Registry`, `score_action`) backing
  the GUI's Find Action palette.
- `util/log.hpp` — header-only logging (`CANVAS_LOG(fmt, …)`, `log_error`,
  `log_warning`, `log_info`, the audio variants); the sibling headers
  `render_telemetry.hpp` and `color_log.hpp` add the telemetry/colour helpers.
  Output is stderr plus a log file (default `~/studio/canvas_debug.log`,
  override with `CANVAS_LOG_FILE`; per-route files such as
  `Canvas-Video.log`/`Canvas-Color.log` land under `~/studio/`). Everything
  except `log_error` is compiled out in a Release build (`NDEBUG`); in a Debug
  build `CANVAS_DEBUG=1` (and `CANVAS_PLAYBACK_DEBUG=1` for playback) turns the
  verbose routes on. `CANVAS_DEBUG` cannot re-enable logging in Release.

## gui/ — the Qt shell

`MainWindow` is the view-controller root: it owns the `SequenceController`,
`ThumbnailService`, `ViewerGL`, `TimelineWidget`, docks, render queue, undo
stack, and the `Project`, and it bridges timeline signals → core edit ops →
snapshot → controller. `MainWindowShell.cpp` is now a short ordered
coordinator (`build_ui()`) that calls the split shell builders — menus, top
bar, page bar, left dock, inspector dock, center workspace — instead of
holding the chrome itself; each builder lives in its own `Shell*.cpp`.
`MainWindow.ui` only supplies the window + three docks.

Behavior is split across `src/features/` as one class in several files:

- `app/AppActions.cpp` — keyboard, close-event unsaved prompt, clip
  enable/transition toggles, media placement.
- `project/ProjectActions.cpp` — import/new/open/save, recent files
  (QSettings, 10 entries), plus the project-manager widget and New Project
  dialog.
- `timeline/TimelineActions.cpp` — `connect_timeline()`: every timeline
  widget signal → core op with undo recording. `SubtitleActions.cpp` adds the
  AI subtitle flow; `view_options_menu.cpp`/`timeline_view_options.*` drive the
  timeline view presets.
- `playback/sequence_controller.*` — the active playback stack, now a thin
  coordinator: a command queue + one worker thread that composes
  `TimelineDecoder` (video decode/composite), `AudioPipeline` (audio
  decode/feed and A/V-sync bookkeeping), and `SonicSync` (drop-to-realtime
  cap). (`playback_controller.*` is the older rendition, still compiled but
  superseded.)
- `playback/audio_output.*` — ALSA with PipeWire fallback, float PCM, one
  active writer thread per backend. Known quirk documented in code: `flush()`
  must join the ALSA thread before dropping to avoid stale audio.
- `thumbnails/thumbnail_service.*` — 4 worker threads, in-memory LRU + disk
  cache (FNV-1a keys), waveform PNGs + raw `.ehwf`.
- `deliver/` — Deliver page UI bound to the core `RenderQueue`:
  `DeliverActions.cpp` (enter/exit + queue actions), `deliver_settings_model.*`
  (headless codec/container lists), `deliver_settings_panel.*`, and
  `render_queue_panel.*`.
- `source_preview/` — the Source viewer: a headless `source_preview_model`
  (single-clip project built from a media entry) plus the Qt panel/controller.
- `color/` — the Color page: `color_page.*`, the `node_graph_canvas`,
  `curves/`, `mini_timeline_strip`, and the `scopes/` (parade, waveform,
  vectorscope, histogram, chromaticity).

### The timeline widget

A `QGraphicsView` facade that owns the scene but does not mutate the model
directly — it emits signals and lets `TimelineActions` do the editing:

- `timeline_widget.*` — facade + geometry constants + signals; holds the
  active tool (`Select`, `Trim`, `Blade`)
- `timeline_view.cpp` — scene drawing: minimap, ruler, timecode bar, tracks,
  filmstrip thumbnails, waveform pixmaps, playhead
- `timeline_interaction.cpp` — blade, drag/select, snap, transition handle
  editor, scrub (ruler-click seek + playhead drag), drag-and-drop
- `timeline_thumbnails.cpp` — thumbnail request/ready state machine
- `timeline_snap.cpp`, `timeline_selection.cpp`, `timeline_drag.cpp`,
  `transition_handle_editor.cpp`, `timeline_volume_line.cpp` — extracted
  Qt-free logic, tested headlessly
- `viewport_selector.*` — hand-painted timebase selector in the transport bar
- `toolbox_widget.*` — the tool/effects toolbox tabs

Visual details worth knowing: the top strip (timecode bar / minimap / ruler)
and its children are *pinned* so they don't scroll, and they're mouse-
transparent so scrubbing works over them; the playhead draws on top. The
divider band between channels is the pan grip — grab it to pull the channels
down into the scroll room; pulling up clamps at the limit.

### Theming

The `theme.*` umbrella sits over split modules: `theme_tokens.*` (design
tokens + `tokens()`/`css()`), `theme_state.*` (dark/light/Hyprland mode,
re-apply callbacks), `theme_clip_colors.*` (Resolve-style clip swatches),
`theme_icons.*` (`SvgIconEngine` tinting), `theme_menu.*` (rounded popup
cards), and `theme_styles.*` (per-widget QSS). `horizon_style.*`
(`HorizonStyle : QProxyStyle`) paints the glassy buttons/toolbars. All
theming is custom — no third-party style library.

## Key flows end to end

**Editing** — widget → `TimelineActions` → core edit op (`ICommand`) →
`UndoStack::record` + `push_snapshot()` (a deep copy of the project handed to
the controller so its worker reads an immutable snapshot).

**Playback** — the controller's worker pops commands, decodes video through
`TimelineDecoder` (per-media decoder + frame cache), presents on `ViewerGL`;
`AudioPipeline` decodes/stretches (WSOLA) and mixes per-clip volume/pan/fades
before writing through `AudioSink`→`AudioOutput`, and `log_av_sync` reports
drift while `SonicSync` caps how far video may run ahead of the audible
position.

**Export** — Deliver panel → `DeliverSettings` → `to_export_settings()` →
`RenderQueue` worker → `export_project()` → `RenderSession` renders frames
(per-clip mix/transform/composite, CUDA NV12 fast path when possible) →
FFmpeg mux (chapters/loudness as configured).

**Hardware decode** — one shared `HwDeviceManager`; `video_decoder`
hw-decodes and downloads with `av_hwframe_transfer_data`.

## Conventions that matter if you touch the code

- **`core/` never includes Qt.** GUI code reaches the engine only through
  headers. Breaking this breaks the headless-seam tests by design.
- **Two decoders overlap** (`VideoDecoder::decode_audio` vs `AudioDecoder`) —
  make sure you know which one a new caller should use.
- **GPU is optional everywhere.** Guard with `CANVAS_HAVE_CUDA` /
  `cuda_available()`; never assume NVENC exists.
- **CMake:** AUTOMOC/AUTORCC/AUTOUIC on, `CMAKE_AUTOUIC_SEARCH_PATHS ui`,
  CUDA compiled with `-allow-unsupported-compiler`. `canvas_core` carries
  `-Wall -Wextra -Wpedantic -Werror` as PUBLIC on GNU/Clang, so the GUI app
  and every test that links it inherit failure-on-warning.
- **Transitions live on the clip**, not in a separate lane.
- **Zero warnings**, always, in every build tree and every test target.