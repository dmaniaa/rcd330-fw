#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_DIR="$ROOT_DIR/qemu"
BUILD_DIR="$QEMU_DIR/build"
QEMU_BIN="$BUILD_DIR/qemu-system-arm"

CLEAN=0
RECONFIGURE=0
HEADLESS=0
EXTRA_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --clean)       CLEAN=1; RECONFIGURE=1 ;;
        --reconfigure) RECONFIGURE=1 ;;
        --headless)    HEADLESS=1 ;;
        -h|--help)
            cat << 'EOF'
Usage: tools/build_qemu.sh [OPTIONS] [-- EXTRA_CONFIGURE_ARGS...]

Configures and builds qemu/build/qemu-system-arm with the RCD 330 machine (-M rcd330).

Options:
  --clean        Remove qemu/build and configure/build from scratch
  --reconfigure  Re-run ../configure even if qemu/build/build.ninja exists
  --headless     Disable GTK/SDL GUI backends (VNC / -display none only)
  -h, --help     Show this help
EOF
            exit 0
            ;;
        *) EXTRA_ARGS+=("$arg") ;;
    esac
done

# 1. Ensure qemu submodule is checked out
if [[ ! -f "$QEMU_DIR/configure" ]]; then
    echo "Initializing qemu submodule..."
    git -C "$ROOT_DIR" submodule update --init qemu
fi

# 2. Check required host tools
MISSING=()
for tool in cc ninja meson pkg-config python3; do
    command -v "$tool" >/dev/null 2>&1 || MISSING+=("$tool")
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "Missing build dependencies: ${MISSING[*]}" >&2
    if [[ "$(uname -s)" == "Darwin" ]]; then
        echo "Install on macOS:  brew install ninja meson pkg-config glib pixman python3" >&2
    else
        echo "Install on Debian/Ubuntu:" >&2
        echo "  sudo apt-get update && sudo apt-get install -y \\" >&2
        echo "    git build-essential ninja-build meson pkg-config \\" >&2
        echo "    python3 python3-venv python3-pip python3-wheel python3-setuptools \\" >&2
        echo "    libglib2.0-dev libpixman-1-dev libslirp-dev libfdt-dev" >&2
    fi
    exit 1
fi

# 3. Clean if requested or if existing binary is for another OS/arch
if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
elif [[ -f "$QEMU_BIN" ]] && ! "$QEMU_BIN" --version >/dev/null 2>&1; then
    echo "Existing $QEMU_BIN cannot run on this host; cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
    RECONFIGURE=1
fi

# 4. Configure if needed
if [[ $RECONFIGURE -eq 1 ]] || [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    mkdir -p "$BUILD_DIR"
    CFG_ARGS=(
        --target-list=arm-softmmu
        --disable-werror
        --disable-docs
    )
    if [[ "$(uname -s)" == "Darwin" ]]; then
        CFG_ARGS+=(--enable-cocoa)
    elif [[ $HEADLESS -eq 1 ]]; then
        CFG_ARGS+=(--disable-gtk --disable-sdl)
    fi
    if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
        CFG_ARGS+=("${EXTRA_ARGS[@]}")
    fi

    echo "Configuring QEMU in $BUILD_DIR..."
    echo "  ../configure ${CFG_ARGS[*]}"
    (cd "$BUILD_DIR" && ../configure "${CFG_ARGS[@]}")
fi

# 5. Build qemu-system-arm (incremental if already built)
echo "Building qemu-system-arm..."
ninja -C "$BUILD_DIR" qemu-system-arm
echo "Ready: $QEMU_BIN"
