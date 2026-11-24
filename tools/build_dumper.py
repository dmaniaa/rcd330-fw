#!/usr/bin/env python3
"""
Build safe, read-only RCD 330 / RCD 360 (Noname/Visteon) dumper update packages.

Leverages opt/sysupdate's unsigned .tar update mechanism (script/before_app_update.sh)
with middleware_update:1 and ZERO [file]:[...] entries so nothing on the headunit's
internal flash/rootfs/PSS/VIP/BT/kernel/bootloader is modified or deleted.

Verified against decompiled 5624/5606 opt/sysupdate:
  - 0x144f0 (FindUpdateTarInDir): requires filename matching std_plus_pss-6.5inch*.tar
  - 0x147fc (IsPackageValid):     extracts opt/stdplus.ini, config.txt, script/
  - 0x12934 (CompareVariety):     matches [Upgrade] PACK_TYPE against device variety
  - 0x11f70 (CompareVersion):     package AP_VER starting with '0' always returns 2 (newer)
  - 0x10fac (ParseConfigFile):    requires >=1 update flag = 1, no blank lines
  - 0x1363c (HostAppUpdate):      0 [file] entries -> extracts nothing, skips rm /pss1/stdplus.ini
  - 0x12ca8 (ScanUsbInDev):       mounts USB/SD read-write at /sysupdate/mntPoint/
"""

from __future__ import annotations

import argparse
import io
import os
import tarfile
import time
from pathlib import Path

PACK_TYPES = [
    "VW_PQ_PSS",
    "SKODA_PQ_PSS",
    "VW_MQB_PSS",
    "SKODA_MQB_PSS",
]

CONFIG_TXT = (
    "kernel_update:0\n"
    "bootloader_update:0\n"
    "vip_update:0\n"
    "bt_update:0\n"
    "middleware_update:1\n"
)

AFTER_APP_UPDATE_SH = """#!/bin/sh
echo after_app_update
sync
exit 0
"""

BEFORE_APP_UPDATE_SH = r"""#!/bin/sh
# RCD 330 / RCD 360 Safe Read-Only Dumper (before_app_update.sh)
# Strictly read-only with respect to internal flash/rootfs/PSS:
# writes ONLY to the external USB/SD mount under rcd360_dump/.

echo "rcd360_dumper: before_app_update start"

# 1. Locate writable external USB/SD mount point
MNT=""
for cand in /sysupdate/mntPoint /media/sda1 /media/sda /media/mmcblk0p1 /media/mmcblk1p1; do
    if [ -d "$cand" ]; then
        if mkdir -p "$cand/rcd360_dump" 2>/dev/null; then
            if echo "ok" > "$cand/rcd360_dump/.rw_test" 2>/dev/null; then
                rm -f "$cand/rcd360_dump/.rw_test" 2>/dev/null
                MNT="$cand"
                break
            fi
        fi
    fi
done

# Fallback: scan /proc/mounts for any mounted sd* or mmcblk* device
if [ -z "$MNT" ] && [ -r /proc/mounts ]; then
    for cand in $(awk '$1 ~ /^\/dev\/(sd|mmcblk)/ { print $2 }' /proc/mounts); do
        if [ -d "$cand" ]; then
            mount -o remount,rw "$cand" 2>/dev/null
            if mkdir -p "$cand/rcd360_dump" 2>/dev/null; then
                if echo "ok" > "$cand/rcd360_dump/.rw_test" 2>/dev/null; then
                    rm -f "$cand/rcd360_dump/.rw_test" 2>/dev/null
                    MNT="$cand"
                    break
                fi
            fi
        fi
    done
fi

if [ -z "$MNT" ]; then
    echo "rcd360_dumper: ERROR no writable USB/SD mount found"
    exit 0
fi

OUT="$MNT/rcd360_dump"
LOG="$OUT/dump.log"
mkdir -p "$OUT" 2>/dev/null

log() {
    echo "[$(date 2>/dev/null || echo 0)] $*" >> "$LOG" 2>/dev/null
    echo "rcd360_dumper: $*"
}

log "=== RCD 330 / RCD 360 Read-Only Dump Start ==="
log "Mount point: $MNT"
log "Output dir:  $OUT"

# 2. Stage 1: System, hardware, kernel, and config snapshot (fast, <2s)
SYSINFO="$OUT/sysinfo.txt"
{
    echo "=== DATE ==="; date 2>&1
    echo "=== UNAME ==="; uname -a 2>&1
    echo "=== CMDLINE ==="; cat /proc/cmdline 2>&1
    echo "=== CPUINFO ==="; cat /proc/cpuinfo 2>&1
    echo "=== MEMINFO ==="; cat /proc/meminfo 2>&1
    echo "=== MTD ==="; cat /proc/mtd 2>&1
    echo "=== PARTITIONS ==="; cat /proc/partitions 2>&1
    echo "=== MOUNTS ==="; cat /proc/mounts 2>&1
    echo "=== DF ==="; df -h 2>&1
    echo "=== LSMOD ==="; lsmod 2>&1
    echo "=== INPUT DEVICES ==="; cat /proc/bus/input/devices 2>&1
    echo "=== I2C SYSFS (PASSIVE) ==="
    for dev in /sys/bus/i2c/devices/*; do
        if [ -e "$dev/name" ]; then
            echo "$dev: $(cat "$dev/name" 2>/dev/null) [$(cat "$dev/modalias" 2>/dev/null)]"
        fi
    done
    echo "=== LTIB RELEASE ==="; cat /etc/ltib-release 2>&1
    echo "=== POINTERCAL ==="; cat /etc/pointercal 2>&1
    echo "=== /opt/stdplus.ini ==="; cat /opt/stdplus.ini 2>&1
    echo "=== /pss1/stdplus.ini ==="; cat /pss1/stdplus.ini 2>&1
    echo "=== /opt/feature_config.ini ==="; cat /opt/feature_config.ini 2>&1
    echo "=== /sysupdate/flag.ini ==="; cat /sysupdate/flag.ini 2>&1
    echo "=== /sysupdate/update_info.ini ==="; cat /sysupdate/update_info.ini 2>&1
    echo "=== LS / ==="; ls -la / 2>&1
    echo "=== LS /dev ==="; ls -la /dev 2>&1
    echo "=== LS /dev/input ==="; ls -la /dev/input 2>&1
    echo "=== LS /opt ==="; ls -la /opt 2>&1
    echo "=== LS /pss1 ==="; ls -la /pss1 2>&1
    echo "=== LS /pss2 ==="; ls -la /pss2 2>&1
    echo "=== LS /sysupdate ==="; ls -la /sysupdate 2>&1
} > "$SYSINFO" 2>&1

dmesg > "$OUT/dmesg.txt" 2>&1
sync
log "Stage 1 complete (sysinfo.txt, dmesg.txt)"

# 3. Stage 2: PSS partitions, /etc, /lib (firmware/modules), /mirrorlink, /sysupdate/*.ini
if [ -d /pss1 ]; then
    log "Archiving /pss1..."
    (cd / && tar -cf "$OUT/pss1.tar" pss1) >> "$LOG" 2>&1
fi
if [ -d /pss2 ]; then
    log "Archiving /pss2..."
    (cd / && tar -cf "$OUT/pss2.tar" pss2) >> "$LOG" 2>&1
fi
if [ -d /etc ]; then
    log "Archiving /etc..."
    (cd / && tar -cf "$OUT/etc.tar" etc) >> "$LOG" 2>&1
fi
if [ -d /lib ]; then
    log "Archiving /lib..."
    (cd / && tar -cf "$OUT/lib.tar" lib) >> "$LOG" 2>&1
fi
if [ -d /mirrorlink ]; then
    log "Archiving /mirrorlink..."
    (cd / && tar -cf "$OUT/mirrorlink.tar" mirrorlink) >> "$LOG" 2>&1
fi
# Save sysupdate metadata files only (NEVER recurse into /sysupdate/mntPoint!)
mkdir -p "$OUT/sysupdate_meta" 2>/dev/null
for f in /sysupdate/flag.ini /sysupdate/update_info.ini; do
    [ -f "$f" ] && cp "$f" "$OUT/sysupdate_meta/" 2>> "$LOG"
done
sync
log "Stage 2 complete (pss1, pss2, etc, lib, mirrorlink)"

# 4. Stage 3: /opt core binaries & libraries (excluding gresfiles first so it's fast)
if [ -d /opt ]; then
    log "Archiving /opt core (excluding gresfiles)..."
    set --
    for p in /opt/*; do
        base="${p#/opt/}"
        case "$base" in
            gresfiles|pss1|pss2) ;;
            *) [ -e "$p" ] && set -- "$@" "$base" ;;
        esac
    done
    if [ $# -gt 0 ]; then
        (cd /opt && tar -cf "$OUT/opt_core.tar" "$@") >> "$LOG" 2>&1
    fi
    sync
    log "Stage 3a complete (opt_core.tar)"
fi

# 5. Stage 4: Small raw MTD partitions (<= 32 MiB: bootloader, kernel, bootlogo, etc.)
if [ -r /proc/mtd ]; then
    mkdir -p "$OUT/mtd" 2>/dev/null
    log "Dumping MTD partitions <= 32 MiB..."
    while read dev size erasesize name; do
        case "$dev" in
            mtd[0-9]*:)
                idx="${dev#mtd}"
                idx="${idx%:}"
                # Strip quotes and unsafe chars from partition label
                clean_name=$(echo "$name" | tr -cd 'A-Za-z0-9._-')
                [ -z "$clean_name" ] && clean_name="part${idx}"
                # Convert hex size to decimal (POSIX arithmetic with printf fallback)
                size_dec=$((0x${size})) 2>/dev/null || size_dec=$(printf '%d' "0x${size}" 2>/dev/null) || size_dec=0
                # Dump partitions <= 32 MiB (33554432 bytes); skip huge rootfs MTD
                if [ "$size_dec" -gt 0 ] && [ "$size_dec" -le 33554432 ]; then
                    dst="$OUT/mtd/mtd${idx}_${clean_name}.bin"
                    log "  Dumping mtd${idx} (${clean_name}, ${size_dec} bytes)..."
                    if command -v nanddump >/dev/null 2>&1; then
                        nanddump -f "$dst" "/dev/mtd${idx}" >> "$LOG" 2>&1
                    elif [ -c "/dev/mtd${idx}ro" ]; then
                        dd if="/dev/mtd${idx}ro" of="$dst" bs=65536 conv=noerror >> "$LOG" 2>&1 || \
                        dd if="/dev/mtd${idx}ro" of="$dst" bs=65536 >> "$LOG" 2>&1
                    elif [ -c "/dev/mtd${idx}" ]; then
                        dd if="/dev/mtd${idx}" of="$dst" bs=65536 conv=noerror >> "$LOG" 2>&1 || \
                        dd if="/dev/mtd${idx}" of="$dst" bs=65536 >> "$LOG" 2>&1
                    else
                        log "  WARNING: /dev/mtd${idx} char device not found"
                    fi
                else
                    log "  Skipping mtd${idx} (${clean_name}, ${size_dec} bytes > 32 MiB)"
                fi
                ;;
        esac
    done < /proc/mtd
    sync
    log "Stage 4 complete (mtd/)"
fi

# 6. Stage 5: Larger directories (/opt/gresfiles and /usr) if space permits
if [ -d /opt/gresfiles ]; then
    log "Archiving /opt/gresfiles..."
    (cd /opt && tar -cf "$OUT/opt_gresfiles.tar" gresfiles) >> "$LOG" 2>&1
    sync
    log "Stage 5a complete (opt_gresfiles.tar)"
fi

if [ -d /usr ]; then
    log "Archiving /usr..."
    (cd / && tar -cf "$OUT/usr.tar" usr) >> "$LOG" 2>&1
    sync
    log "Stage 5b complete (usr.tar)"
fi

log "Final disk usage:"
df -h "$MNT" >> "$LOG" 2>&1
ls -la "$OUT" >> "$LOG" 2>&1
echo "DUMP_OK" > "$OUT/DONE.txt"
sync
log "=== RCD 330 / RCD 360 Read-Only Dump Finished Successfully ==="
sleep 1
exit 0
"""


def make_stdplus_ini(pack_type: str) -> bytes:
    """Create minimal opt/stdplus.ini matching opt/sysupdate's expectations.

    AP_VER starts with '0' so FUN_00011f70 always returns 2 (accepted as newer)
    regardless of the currently installed AP_VER on the headunit.
    """
    content = (
        "[Version]\r\n"
        "AP_VER=0.0.0.1\r\n"
        "CUSTM_PQ=DUMP\r\n"
        "CUSTM_MQB=DUMP\r\n"
        "UBOOT_VER=uboot_0.0.0\r\n"
        "UIMAGE_VER=uImage_0.0.0\r\n"
        "KERNL_VER=0.0.0\r\n"
        "BT_VER = NA\r\n"
        "VIPAP_VER=0.0.0.0\r\n"
        "VIPBL_VER=0.0.0\r\n"
        "[Upgrade]\r\n"
        f"PACK_TYPE={pack_type}\r\n"
        "[CODING]\r\n"
        "BT=0\r\n"
        "CODINGS=0\r\n"
    )
    return content.encode("ascii")


def add_dir(tf: tarfile.TarFile, name: str, mtime: int) -> None:
    ti = tarfile.TarInfo(name=name)
    ti.type = tarfile.DIRTYPE
    ti.mode = 0o755
    ti.uid = 0
    ti.gid = 0
    ti.uname = "root"
    ti.gname = "root"
    ti.mtime = mtime
    tf.addfile(ti)


def add_file(tf: tarfile.TarFile, name: str, data: bytes, mode: int, mtime: int) -> None:
    ti = tarfile.TarInfo(name=name)
    ti.type = tarfile.REGTYPE
    ti.size = len(data)
    ti.mode = mode
    ti.uid = 0
    ti.gid = 0
    ti.uname = "root"
    ti.gname = "root"
    ti.mtime = mtime
    tf.addfile(ti, io.BytesIO(data))


def build_tar(pack_type: str, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    tar_path = out_dir / f"std_plus_pss-6.5inch_dumper_{pack_type}.tar"
    mtime = int(time.time())

    # Use GNU_FORMAT so BusyBox tar handles it cleanly without macOS PAX/xattr headers.
    with tarfile.open(tar_path, "w", format=tarfile.GNU_FORMAT) as tf:
        add_file(tf, "config.txt", CONFIG_TXT.encode("ascii"), 0o644, mtime)
        add_dir(tf, "opt/", mtime)
        add_file(tf, "opt/stdplus.ini", make_stdplus_ini(pack_type), 0o644, mtime)
        add_dir(tf, "script/", mtime)
        add_file(
            tf,
            "script/before_app_update.sh",
            BEFORE_APP_UPDATE_SH.encode("utf-8"),
            0o755,
            mtime,
        )
        add_file(
            tf,
            "script/after_app_update.sh",
            AFTER_APP_UPDATE_SH.encode("utf-8"),
            0o755,
            mtime,
        )

    return tar_path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build safe, read-only RCD 330 / RCD 360 dumper update .tar packages."
    )
    parser.add_argument(
        "--pack-type",
        choices=PACK_TYPES + ["VW_PQ_PSS_HS", "ALL"],
        default="ALL",
        help="Target [Upgrade] PACK_TYPE (default: ALL builds all 4 main variants)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "build" / "dumper",
        help="Output directory for generated .tar files (default: build/dumper)",
    )
    args = parser.parse_args()

    types = PACK_TYPES if args.pack_type == "ALL" else [args.pack_type]
    for pt in types:
        p = build_tar(pt, args.out)
        print(f"Built {p} ({p.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
