#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FW_SRC=""
FORCE=0
USER_OUT_CPIO=""
USER_OUT_ZIMAGE=""
CODING_FILE=""
HMI_SRC=""
PROFILE_ENV=()

while [[ $# -gt 0 ]]; do
    case "$1" in
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
        --force|--rebuild)
            FORCE=1
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
        --out-cpio)
            if [[ $# -lt 2 ]]; then
                echo "Error: --out-cpio requires a path argument" >&2
                exit 1
            fi
            USER_OUT_CPIO="$2"
            shift 2
            ;;
        --out-cpio=*)
            USER_OUT_CPIO="${1#*=}"
            shift
            ;;
        --out-zimage)
            if [[ $# -lt 2 ]]; then
                echo "Error: --out-zimage requires a path argument" >&2
                exit 1
            fi
            USER_OUT_ZIMAGE="$2"
            shift 2
            ;;
        --out-zimage=*)
            USER_OUT_ZIMAGE="${1#*=}"
            shift
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [<firmware.tar>] [--fw <firmware.tar>] [--force] [--out-cpio <path>] [--out-zimage <path>] [--hmi <path>] [--profile-env KEY=VAL]..." >&2
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
STAGE_DIR="$CACHE_DIR/stage"
CACHE_ZIMAGE="$CACHE_DIR/zImage"
CACHE_CPIO="$CACHE_DIR/rootfs.cpio.gz"
FACTORY_BASE_STAGE="$ROOT_DIR/build/factory_base_stage"
FACTORY_BASE_CPIO="$ROOT_DIR/build/factory_base.cpio.gz"
# --hmi / --profile-env build this variant; the stock rootfs.cpio.gz and the
# stage (also the link sysroot for src/hmi) are never modified.
VARIANT_CPIO="$CACHE_DIR/rootfs_hmi.cpio.gz"
WANT_VARIANT=0
if [[ -n "$HMI_SRC" ]] || [[ ${#PROFILE_ENV[@]} -gt 0 ]]; then
    WANT_VARIANT=1
fi

if [[ -n "$HMI_SRC" ]] && [[ ! -f "$HMI_SRC" ]]; then
    echo "Error: --hmi binary not found: $HMI_SRC (build it with tools/build_hmi.sh)" >&2
    exit 1
fi
for kv in ${PROFILE_ENV[@]+"${PROFILE_ENV[@]}"}; do
    if ! [[ "$kv" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]] || [[ "$kv" == *"'"* ]]; then
        echo "Error: --profile-env expects KEY=VAL without single quotes (got: $kv)" >&2
        exit 1
    fi
done

# Variant cpio = stage minus the replaced files, followed by a second cpio
# archive holding the replacements (the kernel unpacks concatenated archives).
# Only regular files go into the overlay archive: a directory entry would
# re-chmod/chown the existing /opt or /etc.
build_variant_cpio() {
    local overlay
    overlay="$(mktemp -d)"
    local find_excl=()

    if [[ -n "$HMI_SRC" ]]; then
        mkdir -p "$overlay/opt"
        cp -f "$HMI_SRC" "$overlay/opt/hmi"
        chmod 755 "$overlay/opt/hmi"
        find_excl+=(! -path ./opt/hmi)
    fi
    if [[ ${#PROFILE_ENV[@]} -gt 0 ]]; then
        mkdir -p "$overlay/etc"
        cp -f "$STAGE_DIR/etc/profile" "$overlay/etc/profile"
        local kv
        for kv in "${PROFILE_ENV[@]}"; do
            printf "export %s='%s'\n" "${kv%%=*}" "${kv#*=}" >> "$overlay/etc/profile"
        done
        chmod 644 "$overlay/etc/profile"
        find_excl+=(! -path ./etc/profile)
        # Nothing in the boot chain (init -> rcS -> launch.sh) reads
        # /etc/profile, so the variant's launch.sh sources it first; pwr_agent
        # and the processes it spawns (hmi) inherit the exports.
        mkdir -p "$overlay/opt"
        { head -n 1 "$STAGE_DIR/opt/launch.sh"; echo ". /etc/profile"; tail -n +2 "$STAGE_DIR/opt/launch.sh"; } \
            > "$overlay/opt/launch.sh"
        chmod 755 "$overlay/opt/launch.sh"
        find_excl+=(! -path ./opt/launch.sh)
    fi

    echo "Creating variant initramfs $VARIANT_CPIO (hmi: ${HMI_SRC:-stock}, profile env: ${#PROFILE_ENV[@]})..."
    rm -f "$VARIANT_CPIO"
    {
        (cd "$STAGE_DIR" && find . "${find_excl[@]}" -print0 | cpio -0 -o -H newc 2>/dev/null)
        (cd "$overlay" && find . -type f -print0 | cpio -0 -o -H newc 2>/dev/null)
    } | gzip -1 > "$VARIANT_CPIO"
    rm -rf "$overlay"
    echo "Done: $VARIANT_CPIO"

    if [[ -n "$USER_OUT_CPIO" ]] && ! [[ "$USER_OUT_CPIO" -ef "$VARIANT_CPIO" ]]; then
        mkdir -p "$(dirname "$USER_OUT_CPIO")"
        cp -f "$VARIANT_CPIO" "$USER_OUT_CPIO"
    fi
}

# A variant only needs the staged stock rootfs; reuse it unless --force.
if [[ "$WANT_VARIANT" -eq 1 ]] && [[ "$FORCE" -eq 0 ]] && [[ -f "$CACHE_CPIO" ]] && \
   [[ -f "$CACHE_ZIMAGE" ]] && [[ -f "$STAGE_DIR/etc/profile" ]]; then
    build_variant_cpio
    exit 0
fi

# Clean up any leftover legacy build artifacts
rm -rf "$ROOT_DIR/build/5624_zImage" \
       "$ROOT_DIR/build/rootfs_5624.cpio.gz" \
       "$ROOT_DIR/build/rootfs_5624.ext2" \
       "$ROOT_DIR/build/rootfs_stage"

BUSYBOX_BIN="${BUSYBOX_BIN:-$ROOT_DIR/tools/prebuilt/busybox-armv5l}"
if [[ ! -x "$BUSYBOX_BIN" ]] && [[ -x "$ROOT_DIR/build/busybox-armv5l" ]]; then
    BUSYBOX_BIN="$ROOT_DIR/build/busybox-armv5l"
fi
if [[ ! -x "$BUSYBOX_BIN" ]]; then
    echo "Missing static busybox at $BUSYBOX_BIN" >&2
    exit 1
fi

# Build or stage ARM guest shims (libEGL/libGLESv2, libhal_gpio_shim, /sbin/init, libQtCore)
"$ROOT_DIR/tools/build_shims.sh"

NEED_FACTORY_BASE=0
if [[ "$FORCE" -eq 1 ]] || [[ ! -f "$FACTORY_BASE_CPIO" ]] || [[ ! -d "$FACTORY_BASE_STAGE/bin" ]]; then
    NEED_FACTORY_BASE=1
else
    for dep in \
        "$ROOT_DIR/tools/build_rootfs.sh" \
        "$ROOT_DIR/tools/build_shims.sh" \
        "$BUSYBOX_BIN" \
        "$ROOT_DIR/build/arm_init/init" \
        "$ROOT_DIR/build/arm_gl/libEGL.so.1" \
        "$ROOT_DIR/build/arm_gl/libGLESv2.so.2" \
        "$ROOT_DIR/build/arm_hal_shim/libhal_gpio_shim.so" \
        "$ROOT_DIR/build/arm_qt/libQtCore.so.4"; do
        if [[ -e "$dep" ]] && [[ "$dep" -nt "$FACTORY_BASE_CPIO" ]]; then
            NEED_FACTORY_BASE=1
            break
        fi
    done
fi

if [[ "$NEED_FACTORY_BASE" -eq 1 ]]; then
    echo "Building Factory Base Layer into $FACTORY_BASE_CPIO..."
    rm -rf "$FACTORY_BASE_STAGE"
    mkdir -p "$FACTORY_BASE_STAGE"/{bin,sbin,usr/bin,usr/sbin,usr/lib,usr/local/qte4.7.4/lib,opt/lib,dev,proc,sys,tmp,pss1,pss2/app_data,sysupdate,mnt/rwfs,root}

    cp "$BUSYBOX_BIN" "$FACTORY_BASE_STAGE/bin/busybox"
    chmod 755 "$FACTORY_BASE_STAGE/bin/busybox"

    BIN_APPLETS=(
        sh ash basename cat chmod chown cp date dd df dirname dmesg echo env expr false grep gzip
        head hostname kill killall ln ls logger mkdir mknod mv pidof ps pwd rm rmdir sed
        sleep sync tail tar test touch true umount uname usleep vi wc which
    )
    SBIN_APPLETS=(
        getty halt ifconfig init insmod klogd lsmod mdev modprobe poweroff
        reboot route rmmod sulogin syslogd
    )
    for app in "${BIN_APPLETS[@]}"; do
        ln -sf busybox "$FACTORY_BASE_STAGE/bin/$app"
    done
    # busybox-armv5l has no 'bash' applet; provide a wrapper so #!/bin/bash scripts (e.g. launch_aa.sh) run
    cat > "$FACTORY_BASE_STAGE/bin/bash" << 'EOF'
#!/bin/sh
exec /bin/ash "$@"
EOF
    chmod 755 "$FACTORY_BASE_STAGE/bin/bash"
    for app in "${SBIN_APPLETS[@]}"; do
        ln -sf ../bin/busybox "$FACTORY_BASE_STAGE/sbin/$app"
    done

    # Wrapper for /bin/mount: no-op UBIFS/remount,ro and skip pseudo-FS only if already mounted
    cat > "$FACTORY_BASE_STAGE/bin/mount" << 'EOF'
#!/bin/sh
case "$*" in
    *"-t ubifs"*|*ubi[0-9]:*|*remount,ro*)
        exit 0
        ;;
esac
for target in /proc /sys /dev /tmp; do
    case " $* " in
        *" $target "|*" $target/"*)
            if grep -q " $target " /proc/mounts 2>/dev/null; then
                exit 0
            fi
            ;;
    esac
done
exec /bin/busybox mount "$@"
EOF
    chmod 755 "$FACTORY_BASE_STAGE/bin/mount"

    # No-op UBI tools so pss_handle_with_recovery in unmodified /etc/rc.d/rcS succeeds
    for ubi_cmd in ubiattach ubidetach ubiformat ubimkvol; do
        cat > "$FACTORY_BASE_STAGE/sbin/$ubi_cmd" << 'EOF'
#!/bin/sh
exit 0
EOF
        chmod 755 "$FACTORY_BASE_STAGE/sbin/$ubi_cmd"
    done

    if [[ -x "$ROOT_DIR/build/arm_init/init" ]]; then
        rm -f "$FACTORY_BASE_STAGE/sbin/init"
        cp "$ROOT_DIR/build/arm_init/init" "$FACTORY_BASE_STAGE/sbin/init"
        chmod 755 "$FACTORY_BASE_STAGE/sbin/init"
    fi
    ln -sf ../bin/busybox "$FACTORY_BASE_STAGE/linuxrc"
    ln -sf sbin/init "$FACTORY_BASE_STAGE/init"

    if [[ -f "$ROOT_DIR/build/arm_gl/libEGL.so.1" ]]; then
        cp -f "$ROOT_DIR/build/arm_gl/libEGL.so.1" "$FACTORY_BASE_STAGE/usr/lib/libEGL.so.1"
        cp -f "$ROOT_DIR/build/arm_gl/libGLESv2.so.2" "$FACTORY_BASE_STAGE/usr/lib/libGLESv2.so.2"
    fi
    if [[ -f "$ROOT_DIR/build/arm_hal_shim/libhal_gpio_shim.so" ]]; then
        cp -f "$ROOT_DIR/build/arm_hal_shim/libhal_gpio_shim.so" "$FACTORY_BASE_STAGE/opt/lib/libhal_gpio_shim.so"
    fi
    if [[ -f "$ROOT_DIR/build/arm_qt/libQtCore.so.4" ]]; then
        cp -f "$ROOT_DIR/build/arm_qt/libQtCore.so.4" "$FACTORY_BASE_STAGE/usr/lib/libQtCore.so.4"
        ln -sf /usr/lib/libQtCore.so.4 "$FACTORY_BASE_STAGE/usr/local/qte4.7.4/lib/libQtCore.so.4"
    fi

    rm -f "$FACTORY_BASE_CPIO"
    (cd "$FACTORY_BASE_STAGE" && find . -print0 | cpio -0 -o -H newc 2>/dev/null | gzip -1 > "$FACTORY_BASE_CPIO")
fi

echo "Staging $FW_ID from $FW_SRC into $STAGE_DIR..."
if [[ -d "$STAGE_DIR" ]]; then
    chmod -R u+rwX "$STAGE_DIR" 2>/dev/null || true
    rm -rf "$STAGE_DIR"
fi
rm -f "$CACHE_ZIMAGE" "$CACHE_CPIO"
mkdir -p "$STAGE_DIR"/{bin,sbin,etc,usr/bin,usr/sbin,dev,proc,sys,tmp,pss1,pss2/app_data,sysupdate,mnt/rwfs,root}

tar -xf "$FW_SRC" -C "$STAGE_DIR"
chmod -R u+rwX "$STAGE_DIR"

if [[ ! -f "$STAGE_DIR/kernelImage" ]]; then
    echo "Error: $FW_SRC does not contain kernelImage (software-only archive?)" >&2
    exit 1
fi

MAGIC="$(head -c 4 "$STAGE_DIR/kernelImage" | od -An -tx1 | tr -d ' \n')"
if [[ "$MAGIC" == "27051956" ]]; then
    echo "Extracting zImage (stripping 64-byte U-Boot uImage header) -> $CACHE_ZIMAGE"
    dd if="$STAGE_DIR/kernelImage" of="$CACHE_ZIMAGE" bs=64 skip=1 status=none
else
    echo "Copying raw zImage -> $CACHE_ZIMAGE"
    cp -f "$STAGE_DIR/kernelImage" "$CACHE_ZIMAGE"
fi
rm -f "$STAGE_DIR/kernelImage"

# Strip installer-only files not needed inside runtime initramfs
rm -rf "$STAGE_DIR/u-boot.bin" "$STAGE_DIR/vipImage.bin" "$STAGE_DIR/config.txt" "$STAGE_DIR/script"

# Overlay Factory Base Layer without writing through destination symlinks
(cd "$FACTORY_BASE_STAGE" && tar -cf - .) | (cd "$STAGE_DIR" && tar -Uxf -)

# Ensure /lib/ld-linux.so.3 is a real file so Linux 3.0.35 execve always finds PT_INTERP
if [[ -f "$STAGE_DIR/lib/ld-2.13.so" ]]; then
    rm -f "$STAGE_DIR/lib/ld-linux.so.3"
    cp "$STAGE_DIR/lib/ld-2.13.so" "$STAGE_DIR/lib/ld-linux.so.3"
    chmod 755 "$STAGE_DIR/lib/ld-linux.so.3"
fi

# Populate /pss1 from /opt defaults (matching rcS first-boot behavior)
for item in stdplus.ini config hmi_autotest.ini; do
    if [[ -e "$STAGE_DIR/opt/$item" ]]; then
        cp -rfp "$STAGE_DIR/opt/$item" "$STAGE_DIR/pss1/"
    fi
done

# Pre-seed persisted headunit coding (/pss1 & /pss2 DIAG_PSS_CODING.pss, stdplus.ini, bt_coding.config)
if [[ -z "$CODING_FILE" ]]; then
    CODING_FILE="$ROOT_DIR/build/coding/${FW_ID}.json"
fi
python3 "$ROOT_DIR/tools/rcd330_coding.py" seed \
    --stage-dir "$STAGE_DIR" \
    --coding-file "$CODING_FILE" \
    --fw-id "$FW_ID"

for so in "$STAGE_DIR"/opt/lib/*.so*; do
    [ -e "$so" ] && ln -sf "/opt/lib/$(basename "$so")" "$STAGE_DIR/usr/lib/$(basename "$so")"
done

cat > "$STAGE_DIR/etc/profile" << 'EOF'
export LD_PRELOAD=/opt/lib/libhal_gpio_shim.so
export LD_LIBRARY_PATH=/usr/local/qte4.7.4/lib:/opt/lib:/opt/lib/bt:/opt/lib/media:/opt/lib/screenshot/libpng:/opt/lib/screenshot/libz:/opt/lib/mirrorlink:/usr/local/lib:/usr/lib
export QTDIR=/usr/local/qte4.7.4
export QT_QWS_FONTDIR=$QTDIR/lib/fonts
export QT_PLUGIN_PATH=$QTDIR/plugins
export QWS_SIZE=800x480
export QWS_MOUSE_PROTO=Tslib:/dev/event1
export QWS_DISPLAY=LinuxFb:/dev/fb0
EOF
chmod 644 "$STAGE_DIR/etc/profile"

ln -sf sbin/init "$STAGE_DIR/init"

echo "Creating initramfs $CACHE_CPIO..."
rm -f "$CACHE_CPIO"
(cd "$STAGE_DIR" && find . -print0 | cpio -0 -o -H newc 2>/dev/null | gzip -1 > "$CACHE_CPIO")
echo "Done: $CACHE_CPIO"

if [[ -n "$USER_OUT_ZIMAGE" ]] && [[ -f "$CACHE_ZIMAGE" ]] && ! [[ "$USER_OUT_ZIMAGE" -ef "$CACHE_ZIMAGE" ]]; then
    mkdir -p "$(dirname "$USER_OUT_ZIMAGE")"
    cp -f "$CACHE_ZIMAGE" "$USER_OUT_ZIMAGE"
fi

if [[ -n "$USER_OUT_CPIO" ]] && ! [[ "$USER_OUT_CPIO" -ef "$CACHE_CPIO" ]]; then
    mkdir -p "$(dirname "$USER_OUT_CPIO")"
    cp -f "$CACHE_CPIO" "$USER_OUT_CPIO"
fi

if [[ "$WANT_VARIANT" -eq 1 ]]; then
    build_variant_cpio
fi
