#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PREBUILT_DIR="$ROOT_DIR/tools/prebuilt"
BUILD_DIR="$ROOT_DIR/build"
FW_LIB=""
for cand in "$BUILD_DIR"/cache/*/stage/lib; do
    if [[ -f "$cand/libc.so.6" ]]; then
        FW_LIB="$cand"
        break
    fi
done

FORCE=0
UPDATE_PREBUILT=0

for arg in "$@"; do
    case "$arg" in
        --force)           FORCE=1 ;;
        --update-prebuilt) UPDATE_PREBUILT=1; FORCE=1 ;;
        -h|--help)
            cat << 'EOF'
Usage: tools/build_shims.sh [--force] [--update-prebuilt]

Builds ARM guest shims (libEGL/libGLESv2, libhal_gpio_shim, /sbin/init) into build/arm_*/.
Uses arm-linux-gnueabi-gcc (or zig cc on macOS) when available, and falls back to
tools/prebuilt/ otherwise.
EOF
            exit 0
            ;;
    esac
done

mkdir -p "$BUILD_DIR/arm_gl" "$BUILD_DIR/arm_hal_shim" "$BUILD_DIR/arm_init" "$BUILD_DIR/arm_qt"

HAVE_ARM_GCC=0
HAVE_ZIG=0
command -v arm-linux-gnueabi-gcc >/dev/null 2>&1 && HAVE_ARM_GCC=1
command -v zig >/dev/null 2>&1 && HAVE_ZIG=1

# 1. EGL / GLES2 software framebuffer shim (gl_fb_shim.c)
GL_SRC="$ROOT_DIR/tools/arm_gl/gl_fb_shim.c"
GL_OUT="$BUILD_DIR/arm_gl/libEGL.so.1"
if [[ $FORCE -eq 1 ]] || [[ ! -f "$GL_OUT" ]] || [[ "$GL_SRC" -nt "$GL_OUT" ]]; then
    if [[ $HAVE_ARM_GCC -eq 1 ]]; then
        echo "Building gl_fb_shim with arm-linux-gnueabi-gcc..."
        arm-linux-gnueabi-gcc -O3 -g0 -ffile-prefix-map="$ROOT_DIR=." \
            -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp \
            -ftree-vectorize -ffast-math \
            -U_TIME_BITS -U_FILE_OFFSET_BITS \
            -shared -fPIC -Wl,-s -Wl,-soname,libEGL.so.1 \
            -o "$GL_OUT" "$GL_SRC"
        cp -f "$GL_OUT" "$BUILD_DIR/arm_gl/libGLESv2.so.2"
    elif [[ $HAVE_ZIG -eq 1 ]]; then
        ZIG_LIB="$(zig env | sed -n 's/.*lib_dir[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
        if [[ -n "$ZIG_LIB" && -d "$ZIG_LIB/libc/include/arm-linux-gnu" ]]; then
            echo "Building gl_fb_shim with zig clang (Cortex-A9 NEON softfp) + zig cc link..."
            GL_OBJ="$BUILD_DIR/arm_gl/gl_fb_shim.o"
            zig clang -target armv7a-linux-gnueabi -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp \
                -O3 -g0 -ffile-prefix-map="$ROOT_DIR=." \
                -ftree-vectorize -ffast-math -ffunction-sections -fdata-sections -fPIC \
                -DNDEBUG -D__GLIBC_MINOR__=13 \
                -U_TIME_BITS -U_FILE_OFFSET_BITS \
                -isystem "$ZIG_LIB/include" \
                -isystem "$ZIG_LIB/libc/include/arm-linux-gnu" \
                -isystem "$ZIG_LIB/libc/include/generic-glibc" \
                -isystem "$ZIG_LIB/libc/include/arm-linux-any" \
                -isystem "$ZIG_LIB/libc/include/any-linux-any" \
                -c "$GL_SRC" -o "$GL_OBJ"
            zig cc -target arm-linux-gnueabi.2.13 -shared -Wl,--gc-sections -Wl,-s -Wl,-soname,libEGL.so.1 \
                -o "$GL_OUT" "$GL_OBJ"
            rm -f "$GL_OBJ"
        else
            echo "Building gl_fb_shim with zig cc..."
            zig cc -target arm-linux-gnueabi.2.13 -mcpu=cortex-a9+neon -mfpu=neon -mfloat-abi=softfp \
                -O3 -g0 -ffile-prefix-map="$ROOT_DIR=." -ftree-vectorize -ffast-math \
                -shared -fPIC -Wl,-s -Wl,-soname,libEGL.so.1 \
                -o "$GL_OUT" "$GL_SRC"
        fi
        cp -f "$GL_OUT" "$BUILD_DIR/arm_gl/libGLESv2.so.2"
    elif [[ -f "$PREBUILT_DIR/libEGL.so.1" ]]; then
        echo "Using prebuilt libEGL.so.1 / libGLESv2.so.2 from tools/prebuilt/..."
        cp -f "$PREBUILT_DIR/libEGL.so.1" "$GL_OUT"
        cp -f "$PREBUILT_DIR/libGLESv2.so.2" "$BUILD_DIR/arm_gl/libGLESv2.so.2"
    else
        echo "Error: cannot build gl_fb_shim (no arm-linux-gnueabi-gcc/zig and no prebuilt)" >&2
        exit 1
    fi
fi

# 2. HAL / VIP MMIO / virtual /dev/event0 LD_PRELOAD shim (hal_gpio_shim.c)
HAL_SRC="$ROOT_DIR/tools/arm_hal_shim/hal_gpio_shim.c"
HAL_OUT="$BUILD_DIR/arm_hal_shim/libhal_gpio_shim.so"
if [[ $FORCE -eq 1 ]] || [[ ! -f "$HAL_OUT" ]] || [[ "$HAL_SRC" -nt "$HAL_OUT" ]]; then
    if [[ $HAVE_ARM_GCC -eq 1 ]] && [[ -f "$FW_LIB/libc.so.6" ]]; then
        echo "Building hal_gpio_shim with arm-linux-gnueabi-gcc (linked against target glibc 2.13)..."
        arm-linux-gnueabi-gcc -O2 -g0 -ffile-prefix-map="$ROOT_DIR=." \
            -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp \
            -U_TIME_BITS -U_FILE_OFFSET_BITS \
            -shared -fPIC -nostdlib -Wl,-s \
            -o "$HAL_OUT" "$HAL_SRC" \
            "$FW_LIB/libdl.so.2" "$FW_LIB/libpthread.so.0" "$FW_LIB/libc.so.6" -lgcc
    elif [[ $HAVE_ZIG -eq 1 ]]; then
        echo "Building hal_gpio_shim with zig cc..."
        zig cc -target arm-linux-gnueabi.2.13 -mcpu=cortex_a9 -O2 -g0 \
            -ffile-prefix-map="$ROOT_DIR=." \
            -shared -fPIC -Wl,-s -o "$HAL_OUT" "$HAL_SRC" -ldl -lpthread
    elif [[ -f "$PREBUILT_DIR/libhal_gpio_shim.so" ]]; then
        echo "Using prebuilt libhal_gpio_shim.so from tools/prebuilt/..."
        cp -f "$PREBUILT_DIR/libhal_gpio_shim.so" "$HAL_OUT"
    else
        echo "Error: cannot build hal_gpio_shim (no arm-linux-gnueabi-gcc/zig and no prebuilt)" >&2
        exit 1
    fi
fi

# 3. Minimal /sbin/init (init.c) — static musl via zig, or prebuilt static binary
INIT_SRC="$ROOT_DIR/tools/arm_init/init.c"
INIT_OUT="$BUILD_DIR/arm_init/init"
if [[ $FORCE -eq 1 ]] || [[ ! -x "$INIT_OUT" ]] || [[ "$INIT_SRC" -nt "$INIT_OUT" ]]; then
    if [[ $HAVE_ZIG -eq 1 ]]; then
        echo "Building static /sbin/init with zig cc (arm-linux-musleabi)..."
        zig cc -target arm-linux-musleabi -O2 -g0 -ffile-prefix-map="$ROOT_DIR=." \
            -static -Wl,-s -o "$INIT_OUT" "$INIT_SRC"
    elif [[ -f "$PREBUILT_DIR/init" ]]; then
        echo "Using prebuilt static /sbin/init from tools/prebuilt/..."
        cp -f "$PREBUILT_DIR/init" "$INIT_OUT"
        chmod 755 "$INIT_OUT"
    else
        echo "Error: cannot build static /sbin/init (install zig or restore tools/prebuilt/init)" >&2
        exit 1
    fi
fi

# 4. Copy prebuilt libQtCore.so.4 if not present in build/arm_qt
if [[ ! -f "$BUILD_DIR/arm_qt/libQtCore.so.4" ]] && [[ -f "$PREBUILT_DIR/libQtCore.so.4" ]]; then
    cp -f "$PREBUILT_DIR/libQtCore.so.4" "$BUILD_DIR/arm_qt/libQtCore.so.4"
fi

if [[ $UPDATE_PREBUILT -eq 1 ]]; then
    echo "Updating tools/prebuilt/..."
    mkdir -p "$PREBUILT_DIR"
    cp -f "$GL_OUT" "$BUILD_DIR/arm_gl/libGLESv2.so.2" "$HAL_OUT" "$INIT_OUT" "$PREBUILT_DIR/"
fi
