# Dependencies

Everything required to build and run Novara Canvas Studio, what it is used for,
and the exact package names per distribution. The short version: this is a
**Linux-only**, C++20 project built with **CMake + Ninja** on top of
**Qt6** and **FFmpeg**, with a handful of optional extras.

Most dependencies come from the system. Two exceptions are pulled in by
CMake itself: **whisper.cpp** is fetched with `FetchContent` at configure time
(pinned to `v1.9.4`, so the first configure needs network access), and
**rnnoise** is vendored in-tree under `core/third_party/rnnoise/` and built as
a static library. Everything else is expected from your system.

## What we actually link against

| Dependency | Required? | What it's used for | Notes |
|---|---|---|---|
| **Qt 6** (Widgets, OpenGLWidgets, Svg, Test) | Yes | The whole GUI shell, timeline, viewer, icons | Tested on 6.11; any recent 6.x works. `Test` supplies the Qt-linked test targets |
| **FFmpeg** (devel) | Yes | Decoding, encoding, muxing, resampling in the core engine | libavformat, libavcodec, libavutil, libswscale, libswresample |
| **nlohmann-json** | Yes | Project file serialize/deserialize (`*.ncs`) | Header-only; included from `/usr/include/nlohmann` |
| **whisper.cpp** | Yes | Local audio transcription (AI Tools → Generate Subtitles) | Fetched by CMake `FetchContent`, pinned to `v1.9.4`; first configure needs network |
| **rnnoise** | Yes | Voice Isolation denoiser | Vendored in-tree (`core/third_party/rnnoise/`), built as a static lib |
| **CMake** ≥ 3.24 | Yes | Build system | Ninja preferred, Makefile fallback |
| **C++20 compiler** | Yes | The codebase | GCC; `-Wall -Wextra -Wpedantic -Werror` (PUBLIC, so it propagates to every target) |
| **Ninja** | Recommended | Build generator | `build.sh` falls back to Unix Makefiles |
| **pkgconf / pkg-config** | Yes | Detects FFmpeg, PipeWire, ALSA, libva, EGL module lists | |
| **CUDA toolkit** (12 or 13) | Optional | NVENC fast path: CPU RGBA → NV12, GPU resize, fused grade + title kernels | Auto-detected via `nvcc`; sets `CANVAS_HAVE_CUDA` |
| **VAAPI** (libva devel) | Optional | Zero-copy VAAPI decode/import + encode path | Detected via `pkg_check_modules(LIBVA … libva)`; sets `CANVAS_HAVE_VAAPI` |
| **EGL** (devel) | Optional | Imports VAAPI DMA-BUF surfaces into the OpenGL viewer | Detected via pkg-config; sets `CANVAS_HAVE_EGL`; only active alongside VAAPI |
| **Vulkan** (headers) | Optional | Enables the extra `Vulkan-tests/` suite only | `find_package(Vulkan QUIET)`; the app does not require it |
| **PipeWire** 0.3 (devel) | Optional | Audio output fallback | Detected via pkg-config; sets `CANVAS_HAVE_PIPEWIRE` |
| **ALSA** (devel) | Optional | Preferred audio output path | Detected via pkg-config; sets `CANVAS_HAVE_ALSA`; on absence, audio playback is disabled |

### Qt modules in detail

Qt's `qtbase` package bundles Widgets, Core, Gui, and importantly the
**OpenGLWidgets** module (the viewer is a `QOpenGLWidget`) — that's why the
package list only needs `qt6-base` + `qt6-svg`:

- `find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg Test)`
- X (the display server) and OpenGL runtime are needed at runtime, not build time.

`Test` supplies `Qt6::Test` for the Qt-linked test targets (`gui/tests/qt/`); it
also ships in `qtbase`, so the package list is unchanged.

MOC/RCC/UIC are handled by CMake's AUTOMOC/AUTORCC/AUTOUIC — no manual step.

### FFmpeg modules

The core engine does the media work with raw FFmpeg C APIs:

- **libavformat** — demuxing/muxing container files
- **libavcodec** — the codecs (H.264/H.265, AAC, etc.)
- **libavutil** — shared helpers, frame/timestamp math
- **libswscale** — pixel format conversion (decode → CPU RGBA)
- **libswresample** — audio conversion/resampling on decode

Enabled hardware encoders at export time are detected at runtime (NVENC,
VAAPI, QSV), so you do not need all of them installed to build.

## Optional extras in detail

**CUDA** — builds the GPU encode path (`canvas::core::gpu` kernels
`rgbaToNV12`, `nv12Resize`, `nv12GradeResize`, and `nv12TitleBlend`, which
fuses title-sprite compositing into the resize/grade launch). The colour maths
is BT.709 (limited range), matching the shared `gpu/colorspace.hpp`. Not on the
PATH by default, so `build.sh` probes `$CUDA_HOME`, `/usr/local/cuda`,
`/opt/cuda`, `/usr/cuda` for `nvcc` and exposes it to the build. The `.cu`
file is compiled with `-allow-unsupported-compiler` because nvcc lags the
newest GCC. Without CUDA the exporter gracefully falls back to CPU encoding.

**VAAPI** — optional zero-copy path, detected via
`pkg_check_modules(LIBVA … libva)` in the core CMakeLists. It defines
`CANVAS_HAVE_VAAPI` and enables the VAAPI decode/import and encode code paths;
absent, those fall back to the CPU / CUDA paths.

**EGL** — optional, detected via `pkg_check_modules(EGL egl)` in the GUI
CMakeLists (`CANVAS_HAVE_EGL`). Pairs with VAAPI to import DMA-BUF surfaces
into the OpenGL viewer; without it the VAAPI viewer import is compiled out.

**Vulkan** — optional; `find_package(Vulkan QUIET)` at the top level only
decides whether the extra `Vulkan-tests/` suite is configured. The app itself
does not require Vulkan.

**PipeWire / ALSA** — audio output. ALSA is preferred, PipeWire used as
fallback. Both are `pkg_check_modules(… QUIET)` in the GUI CMakeLists
(`CANVAS_HAVE_ALSA` / `CANVAS_HAVE_PIPEWIRE`), so a machine with neither
simply has no audio while video still works.

**whisper.cpp / rnnoise** — the two non-system dependencies. whisper.cpp powers
local transcription, rnnoise powers Voice Isolation; both are linked into
`canvas_core` (see the link table above).

## Package lists by distribution

`build.sh -d` installs exactly these (after asking for confirmation and
elevating via `pkexec` or `sudo`). The versioned package names track the
current test machine; drop the old ones in the table below is fine too.

### Fedora / dnf

```
cmake ninja-build meson gcc-c++ pkgconf
qt6-qtbase-devel qt6-qtsvg-devel
ffmpeg-devel
pipewire-devel alsa-lib-devel
nlohmann-json-devel
```

### Arch Linux / pacman

```
cmake ninja meson gcc pkgconf
qt6-base qt6-svg
ffmpeg
pipewire alsa-lib
nlohmann-json
```

### Debian / Ubuntu / apt

```
build-essential cmake ninja-build meson pkg-config
qt6-base-dev libqt6svg6-dev
libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
libpipewire-0.3-dev libasound2-dev
nlohmann-json3-dev
```

### PikaOS

Same as Debian — it ships standard Debian packages.

## Version recap

- C++20 (no exceptions in the editing path)
- CMake ≥ 3.24, Ninja (recommended) or Make
- Qt 6.x — tested against 6.11.2; any current 6.x is fine
- FFmpeg — a recent build (post-FFmpeg-4 era); the code targets the modern
  avcodec API
- CUDA 12 or 13 if you want the NVENC fast path