# Building, running, and testing

This project is deliberately built with boring, standard tooling: CMake,
Ninja, and whatever your distro ships. There's a wrapper script that does the
sensible thing, and the `justfile` for the install/release plumbing.

## Fast path

```sh
./build.sh            # Release build into ./build/ → ./build/gui/canvas
./build/gui/canvas    # launch (optionally: ./build/gui/canvas somefile.mp4)
ctest --test-dir build
```

`build.sh` options:

- `-d, --install-deps` — installs the dependency list from
  [DEPENDENCIES.md](DEPENDENCIES.md) via your package manager (elevates with
  pkexec or sudo, asks for confirmation first)
- `-c, --clean` — wipes `./build` and reconfigures from scratch
- `-t, --type TYPE` — `Release` (default), `Debug`, or `RelWithDebInfo`
- `-j, --jobs N` — parallel build jobs
- `-r, --no-build` — skip the build (useful with `-d` to just install deps)
- `-h, --help` — print the option list

After a successful build, `build.sh` asks whether to install the freshly built
binary system-wide via `pkexec`/`sudo` (`cmake --install` into `/usr`).

Distro detection matters for dependency installs, not for the build itself.
Everything is detected by CMake at configure time. If Ninja is missing,
`build.sh` quietly falls back to Unix Makefiles.

## Doing it manually

If you prefer raw CMake over the wrapper:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Debug builds

Use `./build.sh -t Debug` or pass `-DCMAKE_BUILD_TYPE=Debug`. Two debugging
aids worth knowing:

- **Logging is a Debug-build feature.** Release builds compile the verbose
  routes out — only errors (`log_error`, Qt `qCritical`/`qFatal`) still print.
  In a Debug build, run with `CANVAS_DEBUG=1` to enable logging (it writes to
  stderr plus a log file, default `~/studio/canvas_debug.log`, override the
  file with `CANVAS_LOG_FILE`); `CANVAS_PLAYBACK_DEBUG=1` raises the playback
  verbosity. `CANVAS_DEBUG=1` has no effect in a Release build.
- The repo keeps an **ASan tree** (`build-asan/`, Debug +
  `-fsanitize=address`) for hunting memory bugs in the edit-op / serialization
  half of the engine; the `roundtrip` test is the one that exercises that path.

## The zero-warning rule

This repo treats compiler warnings as build failures. `canvas_core` compiles
with `-Wall -Wextra -Wpedantic -Werror` on GNU/Clang, and because every target
— the GUI app, the core library, and every test — links `canvas_core`, they all
inherit `-Werror`. A warning therefore fails the build outright rather than
being caught by review.

There are three trees to keep clean: `build/` (Release), `build-debug/`
(Debug), and the `build-release/` tree.

## The headless Qt-free guard

`build.sh` runs `scripts/check_qtdep.sh -q` at the end of every build. It
verifies that the so-called headless modules — everything in `core/` plus the
extracted GUI modules (the playback stack, the `audio_targets` /
`deliver_settings_model` / `source_preview_model` seams, and the timeline
interaction math) — contain no `#include <Q...>`. The script's allowlist is
authoritative; keep it in sync when a headless module moves. This seam is what
keeps the engine and the extraction tests buildable without a display. If a
stray Qt include sneaks in, the build fails with a clear message.

## Installing to the system

The default `./build` is a user-local build. To install into `/usr` there's a
dedicated release tree and a `justfile` on top:

```sh
just configure-release   # cmake-configure build-release/ (install prefix /usr)
just build-release       # compile build-release/ only, no install
just install             # configure + build + install into /usr (run as root/sudo)
just install-release     # build-release as your user, then sudo-install
just uninstall           # removes canvas + legacy event-horizon installs
```

`just install` runs `cmake --install` without wrapping it in `sudo`, so it has
to be run as root (or via `sudo just install`); `just install-release` is the
non-root path. There are also `just test`, `just test-core`, and `just test-gui`.

Install layout (matches the `.desktop` launcher):

- `/usr/bin/canvas`
- `/usr/share/applications/canvas.desktop`
- `/usr/share/icons/hicolor/scalable/apps/canvas.svg`
- plus a cleanup of the pre-rename `/usr/bin/event-horizon` and companions on
  `uninstall`

Note: `build-release/` is a separate CMake tree from `build/`. If it's owned
by root (built via `sudo just install` before), rebuild it as root again or
`sudo rm -rf build-release` first.

## Testing

```sh
ctest --test-dir build
```

**77 tests**, split across four families (run `ctest --test-dir build -N` for
the authoritative list). Everything passes on this machine except two Vulkan
render-kernel stubs (below), which SKIP-as-fail and are still WIP.

**Core engine** (`core/tests/`): every test links `canvas_core`, and none of
them need Qt or a display. They cover the model/edit path (`roundtrip`,
`graph`, `graph_edit`, `composite`, `op`, `lut`, `clip_rate`, `equalizer`,
`markers`, `three_point`, `track_ops`, `blend_modes`), colour science
(`colorsci`, `wheels_ui`, `curves`, `histogram`), media/GPU (`gpu_grade`,
`gpu_select`, `vram_leak`, `sw_decode`, `sw_decode_clip`, `voice_isolation`,
`transcribe`, `transcript`), and export (`export_sweep`, `scrub_bench`, `edl`,
`loudness`, `chapters`, `qc`, `autosave`, `queue_policy`, `caption_burn`, the
VAAPI/QSV/CUDA encode tests, and the `sw_encode_bench`/`*_bench` benchmarks).
Some notable behaviours:

- `roundtrip` — edit operations + project (de)serialization round-trip, plus
  the UTF-8/NaN-repair save edges. It is the oldest test in the repo and now
  passes against the current timeline model.
- `export_sweep` — exports every valid codec × container combo into
  `/tmp/canvas_export_sweep/`. Returns 2 (SKIP) if `libx264` isn't available,
  which CTest reports as a normal skip.
- `scrub_bench` — benchmarks the decoder's seek/decode fast path against a
  **calibrated p95 latency budget** (pass/fail) and reports the measured
  numbers; it skips if the synthetic clip's decoder can't open.
- The device-bound tests (`vaapi_enc`, `vaapi_enc_bench`, `cuda_enc`,
  `cuda_enc_bench`, `vram_leak`) skip cleanly (exit 2) when no working
  device/encoder is present.

**GUI headless tests** (`gui/tests/`, 14 tests) — pure-logic modules extracted
from the GUI so they can be tested with **no Qt linked and no display**. The
CMake function `canvas_add_headless_test` is what keeps them honest: it
compiles the exact production source into the test binary, so a stray Qt
include fails the build:

- `sync_constants_test` — shared constant definitions
- `timeline_decoder_test` — playback frame lookup/decoding
- `transition_bake_test` — transition bake math
- `audio_pipeline_test` — audio pipeline state machine (incl. resync fixes)
- `sonicsync_test` — A/V sync logic
- `av_reanchor_test` — the MLT "audio rides with its frame" invariant
- `timeline_snap_test` — zoom-dependent snap quantization math
- `timeline_selection_test` — selection model + linked-mate coalescing
- `timeline_drag_test` — drag-session math, snap-aware, release decisions
- `transition_handle_editor_test` — transition-handle drag session
- `audio_targets_test` — selection → audio-clip target resolution
- `timeline_volume_line_test` — volume-line dB↔y laws
- `deliver_settings_model_test` — codec/container list model
- `source_preview_model_test` — single-clip source-preview project

**GUI Qt-linked tests** (`gui/tests/qt/`, 6 tests) — run offscreen
(`QT_QPA_PLATFORM=offscreen`) with a real widget stack:
`volume_line_drag_qt_test`, `waveform_placement_qt_test`,
`thumbs_id_namespace_test`, `thumbs_disk_serve_test`,
`wheel_panel_roundtrip_qt_test`, `theme_roundtrip_qt_test`.

**Vulkan tests** (`Vulkan-tests/`, 10 tests) — built only when Vulkan headers
are found; skipped at runtime without a Vulkan device. Two of them,
`scrub_bench_vulkan_test` and `visual_render_parity_test`, are still WIP stubs
that SKIP-as-fail (owned by the Vulkan render-kernel work) — treat them as
disabled, not as regressions.

## Launching and what to expect

`./build/gui/canvas` opens the dark, tool-grade UI: a Viewer dock, an Edit
page with a media pool, the Deliver page for export, and the timeline with its
pinned ruler/minimap/timecode strip, linked A/V clips, transitions, snapping,
undo, and the divider band that pans the channels (pull up to the limit, pull
down into the void).

Pass a media file as the first argument to load it straight into the media
pool, or use the app's open dialog.

## Troubleshooting checklists

**CUDA flag not showing in the build summary?** `build.sh` needs `nvcc` on
`PATH`. It probes `$CUDA_HOME`, `/usr/local/cuda`, `/opt/cuda`, `/usr/cuda`.
If your toolkit lives somewhere else, set `CUDA_HOME`.

**Warnings with the newest GCC?** The `.cu` kernel file is compiled with
`-allow-unsupported-compiler` to cope with nvcc lagging behind GCC. If CMake
fails on CUDA, that flag is the fix.

**No audio at runtime?** Means neither ALSA nor PipeWire dev packages were
present at configure time (both are optional). Video playback and export
still work.

**Stale app in menus?** After the rename to Novara Canvas Studio, an earlier
system install may leave an old "Event Horizon" menu entry. `just uninstall`
now removes both the new and the legacy artifacts.