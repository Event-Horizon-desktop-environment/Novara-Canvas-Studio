#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="build"
BUILD_TYPE="Release"
JOBS="$(nproc 2>/dev/null || echo 4)"
INSTALL_DEPS=0
CLEAN_BUILD=0
RUN_BUILD=1
PRINT_CUDA_ENV=0
AUTO_DEPS_APPROVED=0
MISSING_PKGS=()

if [[ -t 1 ]]; then
    BOLD=$'\033[1m'
    DIM=$'\033[2m'
    RESET=$'\033[0m'
    RED=$'\033[1;31m'
    GREEN=$'\033[1;32m'
    YELLOW=$'\033[1;33m'
    BLUE=$'\033[1;34m'
    MAGENTA=$'\033[1;35m'
    CYAN=$'\033[1;36m'
    GRAY=$'\033[90m'
else
    BOLD="" DIM="" RESET="" RED="" GREEN="" YELLOW="" BLUE=""
    MAGENTA="" CYAN="" GRAY=""
fi

info()    { printf "${BLUE}  ▸${RESET} %s\n" "$*"; }
ok()      { printf "${GREEN}  ✓${RESET} %s\n" "$*"; }
warn()    { printf "${YELLOW}  ⚠${RESET} %s\n" "$*"; }
fail()    { printf "${RED}  ✗${RESET} %s\n" "$*"; exit 1; }
step()    { printf "\n${CYAN}${BOLD}  ━━ %s ──${RESET}\n" "$*"; }

hr() {
    local w
    w=$(($(tput cols 2>/dev/null || echo 72) - 4))
    printf "${GRAY}"
    printf '  %.0s─' $(seq 1 "$w")
    printf "${RESET}\n"
}

spinner() {
    local pid=$1 msg=${2:-}
    local frames=('⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏')
    local i=0
    while kill -0 "$pid" 2>/dev/null; do
        printf "\r  ${MAGENTA}%s${RESET} %s" "${frames[i]}" "$msg"
        i=$(( (i + 1) % ${#frames[@]} ))
        sleep 0.1
    done
    wait "$pid" 2>/dev/null
    local rc=$?
    if [[ $rc -eq 0 ]]; then
        printf "\r  ${GREEN}✓${RESET} %s\n" "$msg"
    else
        printf "\r  ${RED}✗${RESET} %s (exit %d)\n" "$msg" "$rc"
        return 1
    fi
}

banner() {
    printf "\n"
    printf "    ${BOLD}${CYAN}Nova Canvas Studio${RESET} ${DIM}— Nonlinear Video Editor${RESET}\n"
    hr
}

detect_distro() {
    step "Detecting Distribution"

    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        DISTRO_ID="${ID:-unknown}"
        DISTRO_LIKE="${ID_LIKE:-$ID}"
        DISTRO_NAME="${PRETTY_NAME:-$ID}"
    else
        fail "Cannot detect distribution: /etc/os-release not found."
    fi

    case "$DISTRO_ID" in
        fedora)             PKG_MGR="dnf"   ; DISTRO_FAMILY="fedora"  ;;
        arch|manjaro|endeavouros|garuda) PKG_MGR="pacman"; DISTRO_FAMILY="arch"    ;;
        debian|ubuntu|linuxmint|pop|pika*)
            if [[ "$DISTRO_ID" == pika* ]] || [[ "$DISTRO_LIKE" == *pika* ]]; then
                PKG_MGR="apt"
                DISTRO_FAMILY="pikaos"
            elif [[ "$DISTRO_LIKE" == *debian* ]] || [[ "$DISTRO_LIKE" == *ubuntu* ]]; then
                PKG_MGR="apt"
                DISTRO_FAMILY="debian"
            else
                PKG_MGR="apt"
                DISTRO_FAMILY="debian"
            fi
            ;;
        *)
            if [[ "$DISTRO_LIKE" == *fedora* ]]; then
                PKG_MGR="dnf"; DISTRO_FAMILY="fedora"
            elif [[ "$DISTRO_LIKE" == *arch* ]]; then
                PKG_MGR="pacman"; DISTRO_FAMILY="arch"
            elif [[ "$DISTRO_LIKE" == *debian* ]] || [[ "$DISTRO_LIKE" == *ubuntu* ]]; then
                PKG_MGR="apt"; DISTRO_FAMILY="debian"
            else
                fail "Unsupported distribution: $DISTRO_ID ($DISTRO_LIKE)."
            fi
            ;;
    esac

    ok "Detected: ${BOLD}$DISTRO_NAME${RESET} (family: $DISTRO_FAMILY, pkg: $PKG_MGR)"
}

FEDORA_DEPS=(
    cmake
    ninja-build
    meson
    gcc-c++
    pkgconf
    pkgconf-pkg-config
    git
    qt6-qtbase-devel
    qt6-qtsvg-devel
    libva-devel
    libglvnd-devel
    pipewire-devel
    alsa-lib-devel
    vulkan-headers
    vulkan-loader-devel
)

FEDORA_JSON_CANDIDATES=(
    json-devel
    nlohmann-json-devel
)

FEDORA_FFMPEG_CANDIDATES=(
    ffmpeg-devel
    ffmpeg-free-devel
)

ARCH_DEPS=(
    cmake
    ninja
    meson
    gcc
    pkgconf
    qt6-base
    qt6-svg
    ffmpeg
    pipewire
    alsa-lib
    nlohmann-json
)

DEBIAN_DEPS=(
    build-essential
    cmake
    ninja-build
    meson
    pkg-config
    qt6-base-dev
    libqt6svg6-dev
    libavformat-dev
    libavcodec-dev
    libavutil-dev
    libswscale-dev
    libswresample-dev
    libpipewire-0.3-dev
    libasound2-dev
    nlohmann-json3-dev
)

PIKAOS_DEPS=("${DEBIAN_DEPS[@]}")

pick_elevator() {
    if command -v pkexec &>/dev/null; then
        ELEV="pkexec"
    elif command -v sudo &>/dev/null; then
        ELEV="sudo"
    else
        fail "Neither pkexec nor sudo found — cannot elevate to install packages."
    fi
}

run_elevated() {
    info "Running: $ELEV $*"
    "$ELEV" "$@" 2>&1 | while IFS= read -r line; do
        printf "    ${DIM}%s${RESET}\n" "$line"
    done
}

first_available_pkg() {
    local cand
    for cand in "$@"; do
        if dnf list --available "$cand" 2>/dev/null | grep -Eq "^${cand}\."; then
            printf '%s\n' "$cand"
            return 0
        fi
    done
    printf '%s\n' "$1"
    return 0
}

pkg_installed() {
    case "$DISTRO_FAMILY" in
        fedora) rpm -q "$1" &>/dev/null ;;
        arch) pacman -Q "$1" &>/dev/null ;;
        debian|pikaos) dpkg -s "$1" &>/dev/null ;;
        *) return 0 ;;
    esac
}

base_dep_list() {
    case "$DISTRO_FAMILY" in
        fedora) printf '%s\n' "${FEDORA_DEPS[@]}" ;;
        arch) printf '%s\n' "${ARCH_DEPS[@]}" ;;
        debian) printf '%s\n' "${DEBIAN_DEPS[@]}" ;;
        pikaos) printf '%s\n' "${PIKAOS_DEPS[@]}" ;;
    esac
}

collect_missing_pkgs() {
    MISSING_PKGS=()
    local p
    while IFS= read -r p; do
        if ! pkg_installed "$p"; then
            MISSING_PKGS+=("$p")
        fi
    done < <(base_dep_list)
    if [[ "$DISTRO_FAMILY" == "fedora" ]]; then
        if ! pkg_installed json-devel && ! pkg_installed nlohmann-json-devel; then
            MISSING_PKGS+=("$(first_available_pkg "${FEDORA_JSON_CANDIDATES[@]}")")
        fi
        if ! pkg_installed ffmpeg-devel && ! pkg_installed ffmpeg-free-devel; then
            MISSING_PKGS+=("$(first_available_pkg "${FEDORA_FFMPEG_CANDIDATES[@]}")")
        fi
    fi
}

probe_cuda() {
    if command -v nvcc &>/dev/null; then
        return 0
    fi
    local -a probe_dirs=()
    if [[ -n "${CUDACXX:-}" ]]; then
        if [[ -x "$CUDACXX" ]]; then
            probe_dirs+=("$(dirname "$CUDACXX")")
        elif [[ -x "$CUDACXX/bin/nvcc" ]]; then
            probe_dirs+=("$CUDACXX/bin")
        fi
    fi
    local env_dir
    for env_dir in "${CUDA_HOME:-}" "${CUDA_PATH:-}"; do
        if [[ -n "$env_dir" && -x "$env_dir/bin/nvcc" ]]; then
            probe_dirs+=("$env_dir/bin")
        fi
    done
    local fixed_dir
    for fixed_dir in /usr/local/cuda/bin /opt/cuda/bin /usr/cuda/bin; do
        probe_dirs+=("$fixed_dir")
    done
    local glob_dir
    for glob_dir in /usr/local/cuda-*/bin /opt/cuda-*/bin; do
        if [[ -x "$glob_dir/nvcc" ]]; then
            probe_dirs+=("$glob_dir")
        fi
    done
    local bin_dir
    for bin_dir in "${probe_dirs[@]}"; do
        if [[ -x "$bin_dir/nvcc" ]]; then
            export PATH="$bin_dir:$PATH"
            if [[ -z "${CUDA_HOME:-}" ]]; then
                export CUDA_HOME="$(dirname "$bin_dir")"
            fi
            info "Found CUDA at $(dirname "$bin_dir") — enabling GPU encode path."
            return 0
        fi
    done
    return 1
}

cuda_hint() {
    if command -v nvidia-smi &>/dev/null; then
        warn "NVIDIA GPU present but no CUDA toolkit (nvcc) found — building without the NVENC GPU fast path."
    else
        info "No CUDA toolkit (nvcc) found — building without the NVENC GPU fast path."
        return 0
    fi
    if [[ "${DISTRO_FAMILY:-}" == "fedora" ]]; then
        info "Install it with ./build.sh -d (offers the NVIDIA CUDA repo + cuda-toolkit) or manually from developer.nvidia.com/cuda-downloads, then rebuild."
    fi
}

cuda_repo_for_fedora() {
    local v
    for v in "${VERSION_ID:-}" 44 43 42 41 40; do
        if [[ -n "$v" ]] && curl -fsI "https://developer.download.nvidia.com/compute/cuda/repos/fedora${v}/x86_64/cuda-fedora${v}.repo" >/dev/null 2>&1; then
            printf '%s\n' "$v"
            return 0
        fi
    done
    return 1
}

install_cuda_fedora() {
    if ! command -v nvidia-smi &>/dev/null; then
        return 0
    fi
    if command -v nvcc &>/dev/null; then
        return 0
    fi
    warn "NVIDIA GPU detected but the CUDA toolkit (nvcc) is not installed."
    info "The NVIDIA .run driver ships the driver only — nvcc, cuda_runtime.h and cudart come from the separate CUDA toolkit."
    read -r -p "  Enable the NVIDIA CUDA repo and install cuda-toolkit (~4GB)? [y/N] " answer || true
    case "${answer,,}" in
        y|yes)
            ;;
        *)
            info "Skipping CUDA toolkit install."
            return 0
            ;;
    esac
    cuda_ver="$(cuda_repo_for_fedora || true)"
    if [[ -z "$cuda_ver" ]]; then
        warn "No reachable NVIDIA CUDA repo for this Fedora release — install the toolkit manually from developer.nvidia.com/cuda-downloads."
        return 0
    fi
    run_elevated dnf config-manager addrepo --from-repofile "https://developer.download.nvidia.com/compute/cuda/repos/fedora${cuda_ver}/x86_64/cuda-fedora${cuda_ver}.repo"
    run_elevated dnf install -y cuda-toolkit
    ok "CUDA toolkit installed."
}

install_deps() {
    step "Installing Dependencies"

    case "$DISTRO_FAMILY" in
        fedora)  local -n deps=FEDORA_DEPS  ;;
        arch)    local -n deps=ARCH_DEPS    ;;
        debian)  local -n deps=DEBIAN_DEPS  ;;
        pikaos)  local -n deps=PIKAOS_DEPS  ;;
    esac

    local -a pkgs=("${deps[@]}")

    if [[ "$DISTRO_FAMILY" == "fedora" ]]; then
        pkgs+=("$(first_available_pkg "${FEDORA_JSON_CANDIDATES[@]}")")
        pkgs+=("$(first_available_pkg "${FEDORA_FFMPEG_CANDIDATES[@]}")")
    fi

    info "Packages to install:"
    for pkg in "${pkgs[@]}"; do
        printf "    ${DIM}•${RESET} %s\n" "$pkg"
    done
    echo

    if [[ "${AUTO_DEPS_APPROVED:-0}" -eq 0 ]]; then
        read -r -p "  ${YELLOW}?${RESET} Install these dependencies now? [y/N] " answer || true
        case "${answer,,}" in
            y|yes)
                ;;
            *)
                warn "Dependency installation cancelled."
                return 1
                ;;
        esac
    fi

    pick_elevator

    case "$PKG_MGR" in
        dnf)
            run_elevated dnf install -y "${pkgs[@]}"
            ;;
        pacman)
            run_elevated pacman -Syu --noconfirm "${pkgs[@]}"
            ;;
        apt)
            run_elevated apt update -qq
            run_elevated apt install -y "${pkgs[@]}"
            ;;
    esac

    if [[ "$DISTRO_FAMILY" == "fedora" ]]; then
        install_cuda_fedora
    fi

    ok "All dependencies installed."
}

install_system() {
    step "Installing Nova Canvas Studio to the system"

    pick_elevator

    local abs_build_dir
    abs_build_dir="$(cd "$BUILD_DIR" && pwd)"

    info "Installing the freshly built binary to /usr (you may be prompted for your password)..."
    run_elevated cmake --install "$abs_build_dir" --prefix /usr --strip

    ok "Installed. Launch it from your app menu or run: ${BOLD}canvas${RESET}"
}

do_build() {
    step "Building Nova Canvas Studio"

    local cmake_args=("-DCMAKE_BUILD_TYPE=$BUILD_TYPE" "-DFETCHCONTENT_QUIET=OFF")
    local generator="Ninja"

    if probe_cuda; then
        if [[ -n "${CUDA_HOME:-}" ]]; then
            cmake_args+=("-DCUDAToolkit_ROOT=$CUDA_HOME")
        fi
    else
        cuda_hint
    fi

    if command -v ninja &>/dev/null; then
        cmake_args+=("-G" "Ninja")
    else
        warn "Ninja not found — falling back to Unix Makefiles."
        cmake_args+=("-G" "Unix Makefiles")
    fi

    if [[ "$CLEAN_BUILD" -eq 1 ]]; then
        info "Cleaning previous build directory..."
        rm -rf "$BUILD_DIR"
    fi

    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        cached_src="$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || true)"
        live_src="$(cd "$(dirname "$0")" && pwd -P)"
        cached_src_canon="$(realpath "$cached_src" 2>/dev/null || printf '%s' "$cached_src")"
        if [[ -n "$cached_src" && "$cached_src_canon" != "$live_src" ]]; then
            warn "Build cache points at $cached_src but this checkout resolves to $live_src."
            read -r -p "  Wipe $BUILD_DIR and reconfigure from scratch? [y/N] " answer || true
            case "${answer,,}" in
                y|yes)
                    info "Removing stale $BUILD_DIR ..."
                    rm -rf "$BUILD_DIR"
                    ;;
                *)
                    fail "Stale build cache — re-run with -c to clean, or build from $cached_src."
                    ;;
            esac
        fi
    fi

    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        info "First-time config: cmake -B $BUILD_DIR ${cmake_args[*]}"
        cfg_status=0
        cmake -B "$BUILD_DIR" "${cmake_args[@]}" 2>&1 | while IFS= read -r line; do
            printf "    ${DIM}%s${RESET}\n" "$line"
        done || cfg_status=$?
        if [[ "$cfg_status" -ne 0 ]]; then
            warn "cmake configure failed (exit $cfg_status)."
            info "Missing Qt6 or system libraries? Install them with: ./build.sh -d -r (or: just deps)"
            fail "Configure step failed."
        fi
    else
        info "Already configured — rebuilding incrementally."
        local cached_type
        cached_type=$(grep -E '^CMAKE_BUILD_TYPE:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2) || true
        if [[ -n "$cached_type" && "$BUILD_TYPE" != "$cached_type" ]]; then
            warn "Re-running cmake to apply build type '$BUILD_TYPE' (cache has '$cached_type')."
            cmake -B "$BUILD_DIR" "${cmake_args[@]}" >/dev/null
        fi
    fi

    info "cmake --build $BUILD_DIR -j$JOBS"
    cmake --build "$BUILD_DIR" -j"$JOBS" 2>&1 | while IFS= read -r line; do
        printf "    ${DIM}%s${RESET}\n" "$line"
    done

    hr
    ok "${BOLD}Build complete!${RESET}"

    info "check_qtdep: verifying no Qt in headless modules"
    if ! "$(dirname "$0")/scripts/check_qtdep.sh" -q; then
        fail "check_qtdep FAILED — a headless module includes Qt"
    fi
    ok "check_qtdep: headless modules are Qt-free"

    printf "\n    ${CYAN}Install Nova Canvas Studio system-wide?${RESET} (installs to /usr via pkexec)"
    read -r -p " [y/N] " answer
    case "${answer,,}" in
        y|yes)
            install_system
            ;;
        *)
            info "System install skipped."
            ;;
    esac

    printf "\n    ${CYAN}Binary:${RESET} ./$BUILD_DIR/gui/canvas\n"
    printf "    ${CYAN}Run:${RESET}    ./$BUILD_DIR/gui/canvas [file]\n\n"
}

usage() {
    cat <<EOF
${BOLD}Usage:${RESET} $(basename "$0") [OPTIONS]

${BOLD}Options:${RESET}
  -d, --install-deps    Install build dependencies via system package manager
  -c, --clean           Remove build directory before configuring
  -r, --no-build        Skip build step (useful with -d only)
  --print-cuda-env      Print export lines exposing any discovered nvcc, then exit
  -t, --type TYPE       Build type: Release, Debug, RelWithDebInfo (default: Release)
  -j, --jobs N          Parallel build jobs (default: $(nproc 2>/dev/null || echo 4))
  -h, --help            Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--install-deps) INSTALL_DEPS=1; shift ;;
        -c|--clean)        CLEAN_BUILD=1; shift ;;
        -r|--no-build)     RUN_BUILD=0; shift ;;
        --print-cuda-env)  PRINT_CUDA_ENV=1; shift ;;
        -t|--type)         BUILD_TYPE="$2"; shift 2 ;;
        -j|--jobs)         JOBS="$2"; shift 2 ;;
        -h|--help)         usage; exit 0 ;;
        *)                 warn "Unknown option: $1"; usage; exit 1 ;;
    esac
done

if [[ "$PRINT_CUDA_ENV" -eq 1 ]]; then
    if probe_cuda >/dev/null 2>&1; then
        cuda_bin="$(dirname "$(command -v nvcc)")"
        cuda_root="$(dirname "$cuda_bin")"
        printf 'export PATH="%s:${PATH}"\n' "$cuda_bin"
        printf 'export CUDA_HOME="%s"\n' "$cuda_root"
        printf 'export CUDAToolkit_ROOT="%s"\n' "$cuda_root"
    fi
    exit 0
fi

banner
detect_distro

if [[ "$INSTALL_DEPS" -eq 0 && "$RUN_BUILD" -eq 1 ]]; then
    collect_missing_pkgs
    if [[ "${#MISSING_PKGS[@]}" -gt 0 ]]; then
        warn "Missing system packages for $DISTRO_FAMILY:"
        for m in "${MISSING_PKGS[@]}"; do
            printf "    ${DIM}•${RESET} %s\n" "$m"
        done
        if [[ -t 0 ]]; then
            read -r -p "  Install them now? [y/N] " answer || true
            case "${answer,,}" in
                y|yes)
                    AUTO_DEPS_APPROVED=1
                    INSTALL_DEPS=1
                    ;;
                *)
                    info "Continuing without them — configure fails if anything required is absent."
                    ;;
            esac
        else
            info "Run ./build.sh -d -r (or: just deps) to install them."
        fi
    fi
fi

if [[ "$INSTALL_DEPS" -eq 1 ]]; then
    install_deps
fi

if [[ "$RUN_BUILD" -eq 1 ]]; then
    do_build
else
    info "Build skipped (--no-build)."
fi

ok "${GREEN}${BOLD}Done!${RESET}"
