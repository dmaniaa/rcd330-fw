#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_BIN="$ROOT_DIR/qemu/build/qemu-system-arm"
GUI_BIN="$ROOT_DIR/build/gui/rcd330_gui"
VIP_PORT="${QEMU_VIP_PORT:-4444}"
VNC_PORT="${QEMU_VNC_PORT:-5900}"
if ! [[ "$VNC_PORT" =~ ^[0-9]+$ ]] || (( VNC_PORT < 5900 )); then
    echo "Error: QEMU_VNC_PORT must be an integer >= 5900 (got: $VNC_PORT)" >&2
    exit 1
fi
VNC_DISPLAY=$((VNC_PORT - 5900))
FW_SRC=""
DRY_RUN=0
REBUILD=0
SD_IMG=""
CODING_FILE=""
HEADLESS=0
MONITOR_SOCK=""
QMP_SOCK=""
HMI_SRC=""
PROFILE_ENV=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fw)
            if [[ $# -lt 2 ]]; then
                echo "Error: --fw requires a path argument" >&2
                exit 1
            fi
            FW_SRC="$2"
            shift 2
            ;;
        --fw=*)
            FW_SRC="${1#*=}"
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --rebuild|--force)
            REBUILD=1
            shift
            ;;
        --coding-file)
            if [[ $# -lt 2 ]]; then
                echo "Error: --coding-file requires a path argument" >&2
                exit 1
            fi
            CODING_FILE="$2"
            shift 2
            ;;
        --coding-file=*)
            CODING_FILE="${1#*=}"
            shift
            ;;
        --sd)
            if [[ $# -lt 2 ]]; then
                echo "Error: --sd requires a path argument" >&2
                exit 1
            fi
            SD_IMG="$2"
            shift 2
            ;;
        --sd=*)
            SD_IMG="${1#*=}"
            shift
            ;;
        --headless)
            HEADLESS=1
            shift
            ;;
        --monitor)
            if [[ $# -lt 2 ]]; then
                echo "Error: --monitor requires a unix socket path" >&2
                exit 1
            fi
            MONITOR_SOCK="$2"
            shift 2
            ;;
        --monitor=*)
            MONITOR_SOCK="${1#*=}"
            shift
            ;;
        --qmp)
            if [[ $# -lt 2 ]]; then
                echo "Error: --qmp requires a unix socket path" >&2
                exit 1
            fi
            QMP_SOCK="$2"
            shift 2
            ;;
        --qmp=*)
            QMP_SOCK="${1#*=}"
            shift
            ;;
        --hmi)
            if [[ $# -lt 2 ]]; then
                echo "Error: --hmi requires a path argument" >&2
                exit 1
            fi
            HMI_SRC="$2"
            shift 2
            ;;
        --hmi=*)
            HMI_SRC="${1#*=}"
            shift
            ;;
        --profile-env)
            if [[ $# -lt 2 ]]; then
                echo "Error: --profile-env requires a KEY=VAL argument" >&2
                exit 1
            fi
            PROFILE_ENV+=("$2")
            shift 2
            ;;
        --profile-env=*)
            PROFILE_ENV+=("${1#*=}")
            shift
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [<firmware.tar>] [--fw <firmware.tar>] [--coding-file <path>] [--dry-run] [--rebuild] [--sd <sd_img>] [--headless] [--monitor <unix-socket>] [--qmp <unix-socket>] [--hmi <path>] [--profile-env KEY=VAL]..." >&2
            exit 1
            ;;
        *)
            if [[ -z "$FW_SRC" ]]; then
                FW_SRC="$1"
                shift
            else
                echo "Unexpected argument: $1" >&2
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$FW_SRC" ]]; then
    FW_SRC="$ROOT_DIR/original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar"
fi

if [[ ! -f "$FW_SRC" || "$FW_SRC" != *.tar ]]; then
    echo "Error: firmware source must be an existing .tar archive (got: $FW_SRC)" >&2
    exit 1
fi

FW_DIR_ABS="$(cd "$(dirname "$FW_SRC")" && pwd -P)"
FW_BASE="$(basename "$FW_SRC" .tar)"
if [[ "$FW_DIR_ABS" == "$ROOT_DIR/original-fw" ]]; then
    FW_ID="$FW_BASE"
else
    PATH_HASH="$(printf '%s' "$FW_DIR_ABS/$FW_BASE.tar" | cksum | awk '{print $1}')"
    FW_ID="${FW_BASE}_${PATH_HASH}"
fi

CACHE_DIR="$ROOT_DIR/build/cache/$FW_ID"
ZIMAGE="$CACHE_DIR/zImage"
STOCK_ROOTFS="$CACHE_DIR/rootfs.cpio.gz"
ROOTFS="$STOCK_ROOTFS"
VARIANT_ARGS=()
if [[ -n "$HMI_SRC" ]]; then
    VARIANT_ARGS+=(--hmi "$HMI_SRC")
fi
for kv in ${PROFILE_ENV[@]+"${PROFILE_ENV[@]}"}; do
    VARIANT_ARGS+=(--profile-env "$kv")
done
if [[ ${#VARIANT_ARGS[@]} -gt 0 ]]; then
    # build_rootfs.sh writes the variant next to (never over) the stock cpio.
    ROOTFS="$CACHE_DIR/rootfs_hmi.cpio.gz"
fi
if [[ -z "$CODING_FILE" ]]; then
    CODING_FILE="$ROOT_DIR/build/coding/${FW_ID}.json"
fi

if [[ "$DRY_RUN" -eq 0 ]]; then
    if [[ ! -x "$QEMU_BIN" ]] || ! "$QEMU_BIN" --version >/dev/null 2>&1; then
        echo "Building QEMU (qemu-system-arm)..."
        "$ROOT_DIR/tools/build_qemu.sh"
    fi

    NEED_STAGE=0
    if [[ "$REBUILD" -eq 1 ]] || [[ ! -f "$ZIMAGE" ]] || [[ ! -f "$STOCK_ROOTFS" ]]; then
        NEED_STAGE=1
    else
        for dep in \
            "$FW_SRC" \
            "$CODING_FILE" \
            "$ROOT_DIR/tools/rcd330_coding.py" \
            "$ROOT_DIR/tools/build_rootfs.sh" \
            "$ROOT_DIR/tools/build_shims.sh" \
            "$ROOT_DIR/tools/arm_gl/gl_fb_shim.c" \
            "$ROOT_DIR/tools/arm_hal_shim/hal_gpio_shim.c" \
            "$ROOT_DIR/tools/arm_init/init.c" \
            "$ROOT_DIR/tools/prebuilt/libEGL.so.1" \
            "$ROOT_DIR/tools/prebuilt/libhal_gpio_shim.so" \
            "$ROOT_DIR/tools/prebuilt/init"; do
            if [[ -e "$dep" ]] && [[ "$dep" -nt "$STOCK_ROOTFS" ]]; then
                NEED_STAGE=1
                break
            fi
        done
    fi

    if [[ "$NEED_STAGE" -eq 1 ]]; then
        echo "Staging firmware $FW_ID from $FW_SRC..."
        BUILD_ARGS=(--fw "$FW_SRC" --coding-file "$CODING_FILE")
        if [[ "$REBUILD" -eq 1 ]]; then
            BUILD_ARGS+=(--force)
        fi
        "$ROOT_DIR/tools/build_rootfs.sh" "${BUILD_ARGS[@]}"
    fi

    if [[ ${#VARIANT_ARGS[@]} -gt 0 ]]; then
        "$ROOT_DIR/tools/build_rootfs.sh" --fw "$FW_SRC" --coding-file "$CODING_FILE" "${VARIANT_ARGS[@]}"
    fi

    NEED_GUI_BUILD=0
    if [[ "$HEADLESS" -eq 1 ]]; then
        NEED_GUI_BUILD=0
    elif [[ "$REBUILD" -eq 1 ]] || [[ ! -x "$GUI_BIN" ]]; then
        NEED_GUI_BUILD=1
    elif [[ -n "$(find "$ROOT_DIR/tools/rcd330_gui" -type f -newer "$GUI_BIN" -print -quit 2>/dev/null)" ]] || \
         [[ "$ROOT_DIR/tools/build_gui.sh" -nt "$GUI_BIN" ]]; then
        NEED_GUI_BUILD=1
    fi

    if [[ "$NEED_GUI_BUILD" -eq 1 ]]; then
        echo "Building native RCD 330 Qt6 GUI ($GUI_BIN)..."
        "$ROOT_DIR/tools/build_gui.sh"
    fi
fi

QEMU_CMD=(
    "$QEMU_BIN"
    -M rcd330
    -smp 1
    -kernel "$ZIMAGE"
    -initrd "$ROOTFS"
)

if [[ -n "$SD_IMG" ]]; then
    QEMU_CMD+=(-drive "if=sd,file=$SD_IMG,format=raw")
fi

QEMU_CMD+=(
    -append "console=ttymxc0,115200 lpj=1000000 rdinit=/sbin/init"
    -display none
)
if [[ "$HEADLESS" -eq 0 ]]; then
    QEMU_CMD+=(-vnc "127.0.0.1:${VNC_DISPLAY}")
fi
QEMU_CMD+=(
    -serial stdio
    -chardev "socket,id=vipctl,host=127.0.0.1,port=${VIP_PORT},server=on,wait=off"
    -global rcd330-vip.chardev=vipctl
)
if [[ -n "$MONITOR_SOCK" ]]; then
    QEMU_CMD+=(-monitor "unix:${MONITOR_SOCK},server,nowait")
fi
if [[ -n "$QMP_SOCK" ]]; then
    QEMU_CMD+=(-qmp "unix:${QMP_SOCK},server,nowait")
fi

GUI_CMD=(
    "$GUI_BIN"
    --vip-host 127.0.0.1
    --vip-port "$VIP_PORT"
    --vnc-host 127.0.0.1
    --vnc-port "$VNC_PORT"
    --fw-name "$FW_ID"
    --coding-file "$CODING_FILE"
    --coding-script "$ROOT_DIR/tools/rcd330_coding.py"
)

if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "FW_ID=$FW_ID"
    echo "ZIMAGE=$ZIMAGE"
    echo "ROOTFS=$ROOTFS"
    printf '%q ' "${QEMU_CMD[@]}"
    printf '\n'
    if [[ "$HEADLESS" -eq 0 ]]; then
        printf '%q ' "${GUI_CMD[@]}"
        printf '\n'
    fi
    exit 0
fi

if [[ "$HEADLESS" -eq 1 ]]; then
    echo "Starting headless QEMU RCD 330 ($FW_ID, rootfs $(basename "$ROOTFS"), VIP 127.0.0.1:${VIP_PORT})..."
    exec "${QEMU_CMD[@]}"
fi

echo "Starting QEMU RCD 330 ($FW_ID)..."
echo "Launching native RCD 330 window (VNC 127.0.0.1:${VNC_PORT}, VIP 127.0.0.1:${VIP_PORT})..."
echo "Serial TTY (ttymxc0 / UART1) will appear below once kernel switches from earlyprintk (~5s)..."

"${QEMU_CMD[@]}" &
QEMU_PID=$!
cleanup() {
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

"${GUI_CMD[@]}" --qemu-pid "$QEMU_PID"
