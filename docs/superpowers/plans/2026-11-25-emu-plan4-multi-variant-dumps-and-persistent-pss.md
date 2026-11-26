# Emulator Plan 4: Multi-Variant Device Dumps (`5606`/`5814`), Persistent `/pss` & Virtual `/dev/mtd5`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable QEMU to boot raw headunit NAND/rootfs dumps (such as `extracted-fw/rcd780_dump/` / `5814` MQB) alongside retail `.tar` packages, optionally layer genuine device base libraries (`libQtCore.so.4.7.4`), persist `/pss1`+`/pss2` across reboots via `uSDHC2`, and provide a writable virtual `/dev/mtd5` splash partition.

**Architecture:** Today `tools/build_rootfs.sh` and `tools/run_rcd330.sh` only accept `.tar` archives containing `kernelImage`, overwrite `libQtCore.so.4` with a stand-in prebuilt, keep `/pss1`+`/pss2` in volatile initramfs RAM (wiping all settings, presets, and live coding writes on every reboot), and leave `/dev/mtd5` absent (breaking MQB Byte 18 bootlogo flashing in `VDiagProxy 0x1fb9b8` and SD splash flashing in `VETMHmiLookFeelPage 0x2290f8`). We extend `build_rootfs.sh` / `run_rcd330.sh` to accept dump directories (`mtd/mtd1.bin` + `rootfs/`), add an optional `--base-rootfs` flag for genuine base OS libraries while preserving the public prebuilt default, wire `uSDHC2` (`mmcblk1`) in `qemu/hw/arm/rcd330.c` + `tools/arm_init/init.c` for an optional persistent ext2 `--pss-img`, and emulate `/dev/mtd5` (`MEMGETBADBLOCK` `0x40084d0b`, `MEMERASE` `0x40084d02`, 2048 B `pread`/`pwrite`) in `hal_gpio_shim.c` backed by a persistent 12 MB image file.

**Tech Stack:** Bash (`tools/build_rootfs.sh`, `tools/run_rcd330.sh`), C (`qemu/hw/arm/rcd330.c`, `tools/arm_init/init.c`, `tools/arm_hal_shim/hal_gpio_shim.c`), Python 3 (`tools/rcd330_coding.py`, `tools/tests/test_emu_plan4_variants_and_persistence.py`).

**Spec:** `docs/architecture.md`, `docs/coding.md`, `docs/firmware-packaging-and-update.md`, `docs/diff-5624-vs-rcd780.md`, `docs/decompiled/5624/hmi/functions/0x1fb4dc.c`, `docs/decompiled/5624/hmi/functions/0x1fb7f0.c`, `docs/decompiled/5624/hmi/functions/0x1fb9b8.c`.

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- All public users only have retail `.tar` archives (`5624_vw`, `5606_skoda`); `--base-rootfs`, dump-directory booting, `--pss-img`, and `--mtd5-img` must all be strictly additive/optional so existing `.tar` commands keep working unchanged.
- `uSDHC1` (`IF_SD, 0, 0` → `mmcblk0`) MUST remain reserved for the user's `--sd <media.img>` card; persistent `/pss` storage uses `uSDHC2` (`IF_SD, 0, 1` → `mmcblk1`) so `--sd` and `--pss-img` work simultaneously.
- Preserve unmodified factory `/etc/pointercal` and `/lib/firmware/maxtouch.cfg` across all firmware variants (`5624_vw`, `5606_skoda`, `5814` dump).
- Keep `tools/prebuilt/` (`init`, `libhal_gpio_shim.so`) updated via `./tools/build_shims.sh --force --update-prebuilt` after editing guest C shims.

## Review Focus

- **`5814` `stdplus.ini` dual `BT_VER` keys**: `5814` has `BT_VER=C370` in `[Version]` and `BT_VER=C210` in `[VIP]`. `read_bt_ver_from_ini()` in `hal_gpio_shim.c` must prefer `[VIP]` `BT_VER` when present (falling back to `[Version]` `BT_VER`, then `0xC210`), otherwise `pwr_agent` receives the wrong BT version; pinned in Task 1's test.
- **Dump-directory symlink safety & non-destructive staging**: Copying a raw `rootfs/` dump into `build/cache/<id>/stage` must preserve symlinks (e.g. `libQtCore.so.4 -> libQtCore.so.4.7.4`), never mutate the source dump directory, and strip uImage headers from `mtd/mtd1.bin` (`27051956` magic, even when padded to the 12 MB MTD partition size); pinned in Task 1's test.
- **Read-only vs fresh `--pss-img` initialization**: When `--pss-img <path>` points to a brand-new or empty image, `build_rootfs.sh` / `init.c` must seed `/pss1` and `/pss2` from the staged defaults on first mount (matching `rcS` first-boot behavior), whereas an existing populated `--pss-img` must NOT be overwritten so previous settings/coding survive reboot; pinned in Task 3's test.
- **Virtual `/dev/mtd5` bounds & `MEMERASE` semantics**: `VDiagProxy` (`0x1fb4dc`/`0x1fb7f0`) issues `MEMGETBADBLOCK` (`0x40084d0b`) expecting `0` (good block) and `MEMERASE` (`0x40084d02`) with `struct erase_info_user { uint32_t start; uint32_t length; }` expecting erased bytes to read back as `0xFF`, followed by 2048 B page `pread`/`pwrite` verify. Out-of-bounds erase/write past 12 MB (`0xC00000`) must return `-1` (`EINVAL`); pinned in Task 4's test.
- **`--base-rootfs` missing `libQtCore.so.4.7.4` error handling**: If a user passes `--base-rootfs <dir>` that does not contain `usr/local/qte4.7.4/lib/libQtCore.so.4.7.4`, `build_rootfs.sh` must fail fast with a clear error rather than silently staging a broken mix; pinned in Task 2's test.

---

### Task 1: Support Raw Device Dump Directories (`mtd/mtd1.bin` + `rootfs/`) & Fix `stdplus.ini` Section Parsing

**Files:**
- Modify: `tools/build_rootfs.sh:108-135,330-375`
- Modify: `tools/run_rcd330.sh:140-165`
- Modify: `tools/arm_hal_shim/hal_gpio_shim.c:214-239`
- Modify: `tools/rcd330_coding.py:180-195`
- Create: `tools/tests/test_emu_plan4_variants_and_persistence.py`

**Interfaces:**
- Consumes: Either `<firmware.tar>` OR `<dump_dir>` containing `mtd/mtd1.bin` + `rootfs/` (or `dump/mtd/mtd1.bin` + `dump/rootfs/`).
- Produces:
  - Auto-detection in `tools/build_rootfs.sh` and `tools/run_rcd330.sh` accepting both `.tar` files and raw dump directories, trimming `mtd1.bin` uImage to exact `64 + ih_size` bytes (`be32` at offset `+12`) before stripping the 64-byte U-Boot header into `$CACHE_ZIMAGE`.
  - Section-aware `read_bt_ver_from_ini()` in `tools/arm_hal_shim/hal_gpio_shim.c` preferring `[VIP] BT_VER` over `[Version] BT_VER`.
  - Per-variant default detection in `tools/rcd330_coding.py` (`default_coding_config(fw_id)`) recognizing `MQB` / `5814` / `RCD780` in addition to `SKODA` / `VW`.

- [ ] **Step 1: Write failing tests in `tools/tests/test_emu_plan4_variants_and_persistence.py`**

```python
#!/usr/bin/env python3
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestEmuPlan4VariantsAndPersistence(unittest.TestCase):
    def test_build_rootfs_and_run_accept_dump_directory_and_parse_uimage_size(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertIn("mtd1.bin", build_sh)
        self.assertIn("-d \"$FW_SRC\"", build_sh)
        self.assertIn("-d \"$FW_SRC\"", run_sh)

    def test_hal_shim_prefers_vip_section_bt_ver(self):
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        fn = shim_c.split("read_bt_ver_from_ini(")[1].split("\n}\n")[0]
        self.assertIn("[VIP]", fn)
        self.assertIn("in_vip", fn)

    def test_run_dry_run_accepts_synthetic_dump_dir(self):
        with tempfile.TemporaryDirectory() as td:
            dump = pathlib.Path(td) / "demo_5814_dump"
            (dump / "mtd").mkdir(parents=True)
            (dump / "rootfs/opt").mkdir(parents=True)
            # Minimal valid 64-byte uImage header with ih_size=16 + 16 payload bytes + 1024 padding
            hdr = struct.pack(">IIII", 0x27051956, 0, 0, 16) + b"\x00" * 48
            (dump / "mtd/mtd1.bin").write_bytes(hdr + b"Z" * 16 + b"\xff" * 1024)
            out = subprocess.check_output(
                [str(ROOT / "tools/run_rcd330.sh"), "--fw", str(dump), "--headless", "--dry-run"],
                text=True,
            )
            self.assertIn("FW_ID=demo_5814_dump_", out)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v`
Expected: FAIL (`build_rootfs.sh`/`run_rcd330.sh` reject directories with `"must be an existing .tar archive"`).

- [ ] **Step 3: Implement dump-directory staging, uImage size extraction, and `[VIP]` `BT_VER` parsing**

1. In `tools/run_rcd330.sh` and `tools/build_rootfs.sh`:
   - Accept either `[[ -f "$FW_SRC" && "$FW_SRC" == *.tar ]]` OR `[[ -d "$FW_SRC" ]]` (resolving `$DUMP_MTD` and `$DUMP_ROOTFS` at `$FW_SRC/{mtd,rootfs}`, `$FW_SRC/dump/{mtd,rootfs}`, or `$FW_SRC/sysupdate_emu/dump/{mtd,rootfs}`).
   - When staging from a dump directory in `tools/build_rootfs.sh`:
     - Extract `$CACHE_ZIMAGE` from `$DUMP_MTD/mtd1.bin`: if magic is `27051956`, read big-endian `ih_size` at bytes `12..15` via Python/od and copy exact `ih_size` bytes starting at offset `64` (`dd if=... bs=1 skip=64 count=$IH_SIZE` or `python3 -c`), avoiding trailing NAND `0xFF` padding.
     - Copy `$DUMP_ROOTFS/.` into `$STAGE_DIR` using `(cd "$DUMP_ROOTFS" && tar -cf - .) | (cd "$STAGE_DIR" && tar -xf -)` so symlinks and permissions are preserved without touching the source dump.
     - Preserve existing `/pss1` and `/pss2` files from the dump before running `rcd330_coding.py seed` (only writing defaults if missing, unless `--coding-file` was explicitly provided).
2. In `tools/arm_hal_shim/hal_gpio_shim.c` (`read_bt_ver_from_ini`):
   - Track `[section]` headers (`int in_vip = 0;`) and record `ver_fallback` from `[Version]` and `ver_vip` from `[VIP]`, returning `ver_vip ? ver_vip : (ver_fallback ? ver_fallback : 0xC210u)`.
   - Rebuild prebuilt shims: `./tools/build_shims.sh --force --update-prebuilt`.
3. In `tools/rcd330_coding.py` (`default_coding_config`):
   - Detect `"MQB"` or `"5814"` or `"RCD780"` in `fw_id.upper()` to default `platform` appropriately when `--coding-file` does not exist yet (while keeping `5624_vw` → PQ VW and `5606_skoda` → PQ SKODA).

- [ ] **Step 4: Run tests**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v && python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/build_rootfs.sh tools/run_rcd330.sh tools/rcd330_coding.py tools/arm_hal_shim/hal_gpio_shim.c tools/prebuilt/libhal_gpio_shim.so tools/tests/test_emu_plan4_variants_and_persistence.py
git commit -m "tools: support raw NAND/rootfs device dumps (5814) and section-aware BT_VER parsing"
```

---

### Task 2: Optional Genuine Device Base-OS Layer (`--base-rootfs`) in `tools/build_rootfs.sh`

**Files:**
- Modify: `tools/build_rootfs.sh:10-95,315-365`
- Modify: `tools/run_rcd330.sh:20-125,205-220`
- Modify: `tools/tests/test_emu_plan4_variants_and_persistence.py`

**Interfaces:**
- Consumes: `--base-rootfs <dir>` pointing to a real device `rootfs/` (e.g. `extracted-fw/rcd780_dump/sysupdate_emu/dump/rootfs`).
- Produces:
  - Validation in `tools/build_rootfs.sh`: verifies `$BASE_ROOTFS/usr/local/qte4.7.4/lib/libQtCore.so.4.7.4` exists (failing fast with clear error otherwise).
  - Stages genuine `usr/local/qte4.7.4/lib/libQtCore.so.4.7.4` (+ symlinks `libQtCore.so.4`, `libQtCore.so.4.7`) and genuine `lib/libfreetype*` / `lib/libts*` from `$BASE_ROOTFS` into `$STAGE_DIR` **before** overlaying the retail `.tar` and guest shims (`libEGL.so.1`, `libGLESv2.so.2`, `libhal_gpio_shim.so`, `/sbin/init`), using a separate cache tag (`${FW_ID}_base`) so the default public-tar cache stays untouched.

- [ ] **Step 1: Add failing tests for `--base-rootfs` validation and cache isolation**

Append to `TestEmuPlan4VariantsAndPersistence` in `tools/tests/test_emu_plan4_variants_and_persistence.py`:

```python
    def test_build_rootfs_base_rootfs_fails_fast_when_qtcore_missing(self):
        with tempfile.TemporaryDirectory() as td:
            res = subprocess.run(
                [str(ROOT / "tools/build_rootfs.sh"), "--base-rootfs", td],
                capture_output=True, text=True,
            )
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("libQtCore.so.4.7.4", res.stderr)

    def test_run_rcd330_accepts_base_rootfs_flag(self):
        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertIn("--base-rootfs", run_sh)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v`
Expected: FAIL (`Unknown option: --base-rootfs`).

- [ ] **Step 3: Implement `--base-rootfs` in `tools/build_rootfs.sh` and `tools/run_rcd330.sh`**

1. Add `--base-rootfs <dir>` / `--base-rootfs=<dir>` option parsing to both scripts.
2. In `tools/build_rootfs.sh`, validate:
```bash
if [[ -n "$BASE_ROOTFS" ]]; then
    if [[ ! -f "$BASE_ROOTFS/usr/local/qte4.7.4/lib/libQtCore.so.4.7.4" ]]; then
        echo "Error: --base-rootfs missing usr/local/qte4.7.4/lib/libQtCore.so.4.7.4 (got: $BASE_ROOTFS)" >&2
        exit 1
    fi
    FW_ID="${FW_ID}_base"
fi
```
3. After overlaying `$FACTORY_BASE_STAGE` into `$STAGE_DIR` (line 359), if `[[ -n "$BASE_ROOTFS" ]]`, copy the real `libQtCore.so.4.7.4` (replacing the stand-in `libQtCore.so.4`) and recreate symlinks:
```bash
if [[ -n "$BASE_ROOTFS" ]]; then
    rm -f "$STAGE_DIR/usr/lib/libQtCore.so.4" "$STAGE_DIR/usr/local/qte4.7.4/lib/libQtCore.so.4"*
    cp -f "$BASE_ROOTFS/usr/local/qte4.7.4/lib/libQtCore.so.4.7.4" "$STAGE_DIR/usr/local/qte4.7.4/lib/libQtCore.so.4.7.4"
    ln -sf libQtCore.so.4.7.4 "$STAGE_DIR/usr/local/qte4.7.4/lib/libQtCore.so.4"
    ln -sf /usr/local/qte4.7.4/lib/libQtCore.so.4.7.4 "$STAGE_DIR/usr/lib/libQtCore.so.4"
fi
```
4. Pass `--base-rootfs "$BASE_ROOTFS"` from `tools/run_rcd330.sh` to `tools/build_rootfs.sh` and mirror the `_base` suffix in `FW_ID`.

- [ ] **Step 4: Run tests**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/build_rootfs.sh tools/run_rcd330.sh tools/tests/test_emu_plan4_variants_and_persistence.py
git commit -m "tools: add optional --base-rootfs flag to test against genuine device libQtCore.so.4.7.4"
```

---

### Task 3: Persistent `/pss1` + `/pss2` Backing via `uSDHC2` (`--pss-img <ext2.img>`)

**Files:**
- Modify: `qemu/hw/arm/rcd330.c:332-345`
- Modify: `tools/arm_init/init.c:10-55`
- Modify: `tools/run_rcd330.sh:15-75,240-260`
- Modify: `tools/prebuilt/init` (via `tools/build_shims.sh`)
- Modify: `tools/tests/test_emu_plan4_variants_and_persistence.py`

**Interfaces:**
- Consumes: `-drive if=sd,index=1,file=<pss.img>,format=raw` on QEMU command line.
- Produces:
  - `uSDHC2` (`&s->soc.usdhc[1]`, `mmcblk1`) attached to `drive_get(IF_SD, 0, 1)` in `qemu/hw/arm/rcd330.c` (while `drive_get(IF_SD, 0, 0)` stays on `uSDHC1` for `--sd`).
  - `tools/arm_init/init.c` checks for `/dev/mmcblk1` (creating device node `179, 8` or waiting up to 1 s for block registration): if present and mountable as ext2 on `/mnt/rwfs`, seeds `/mnt/rwfs/pss1` and `/mnt/rwfs/pss2` from initramfs `/pss1` and `/pss2` **only when missing**, then bind-mounts `/mnt/rwfs/pss1` → `/pss1` and `/mnt/rwfs/pss2` → `/pss2` before invoking `/etc/rc.d/rcS`.
  - `--pss-img <path>` flag in `tools/run_rcd330.sh` (creating a blank 32 MB ext2 image automatically if `<path>` does not exist yet via `mke2fs`/`mkfs.ext2` or Python helper).

- [ ] **Step 1: Add failing tests for `uSDHC2` wiring, `--pss-img` CLI flag, and `init.c` bind-mount**

Append to `TestEmuPlan4VariantsAndPersistence` in `tools/tests/test_emu_plan4_variants_and_persistence.py`:

```python
    def test_rcd330_wires_usdhc2_and_run_exposes_pss_img(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        self.assertIn("drive_get(IF_SD, 0, 1)", rcd330_c)
        self.assertIn("s->soc.usdhc[1]", rcd330_c)
        init_c = (ROOT / "tools/arm_init/init.c").read_text()
        self.assertIn("mmcblk1", init_c)
        self.assertIn("MS_BIND", init_c)
        out = subprocess.check_output(
            [str(ROOT / "tools/run_rcd330.sh"), "--headless", "--dry-run", "--pss-img", "/tmp/test_pss.img"],
            text=True,
        )
        self.assertIn("if=sd,index=1,file=/tmp/test_pss.img", out)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v`
Expected: FAIL (`drive_get(IF_SD, 0, 1)` and `--pss-img` not present).

- [ ] **Step 3: Wire `uSDHC2` in `rcd330.c`, bind-mount in `init.c`, and add `--pss-img` to `run_rcd330.sh`**

1. In `qemu/hw/arm/rcd330.c` (right after the `uSDHC1` block at line 343):
```c
    /* Attach optional second SD drive (index=1) to uSDHC2 (mmcblk1) for persistent /pss */
    {
        DriveInfo *dinfo = drive_get(IF_SD, 0, 1);
        if (dinfo) {
            DeviceState *card = qdev_new(TYPE_SD_CARD);
            qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                    &error_fatal);
            qdev_realize_and_unref(
                card, qdev_get_child_bus(DEVICE(&s->soc.usdhc[1]), "sd-bus"),
                &error_fatal);
        }
    }
```
2. In `tools/arm_init/init.c`:
   - Add `mknod("/dev/mmcblk0", S_IFBLK | 0660, makedev(179, 0));` and `mknod("/dev/mmcblk1", S_IFBLK | 0660, makedev(179, 8));` (and check `/sys/block/mmcblk1/dev` dynamically in case `mmcblk0` is absent and `uSDHC2` enumerates as `mmcblk0` — identify the `uSDHC2` block device via `/sys/devices/platform/sdhci-esdhc-imx.1/mmc_host/*/mmc*/block/mmcblk*/dev`!).
   - Mount that block device on `/mnt/rwfs` (`ext2`/`ext4`, `rw,sync`).
   - If `/mnt/rwfs/pss1/stdplus.ini` does not exist, run `/bin/cp -a /pss1/. /mnt/rwfs/pss1/` and `/bin/cp -a /pss2/. /mnt/rwfs/pss2/` (creating directories first).
   - `mount("/mnt/rwfs/pss1", "/pss1", NULL, MS_BIND, NULL);`
   - `mount("/mnt/rwfs/pss2", "/pss2", NULL, MS_BIND, NULL);`
3. In `tools/run_rcd330.sh`:
   - Add `--pss-img <path>` / `--pss-img=<path>`.
   - When non-empty: if `DRY_RUN == 0` and file does not exist, create a 32 MB ext2 image (`dd if=/dev/zero bs=1M count=32 of="$PSS_IMG"` + `mke2fs -F -t ext2 "$PSS_IMG"`). Append `-drive "if=sd,index=1,file=$PSS_IMG,format=raw"` (and update existing `--sd` to explicitly specify `if=sd,index=0,file=$SD_IMG,format=raw`).
4. Rebuild QEMU and prebuilt `/sbin/init`:
   Run: `./tools/build_qemu.sh && ./tools/build_shims.sh --force --update-prebuilt`

- [ ] **Step 4: Run tests**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v && python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/arm/rcd330.c
git -C qemu commit -m "hw/arm/rcd330: attach optional IF_SD index=1 drive to uSDHC2 for persistent /pss"
git add tools/arm_init/init.c tools/prebuilt/init tools/run_rcd330.sh tools/tests/test_emu_plan4_variants_and_persistence.py
git commit -m "tools: add persistent --pss-img (uSDHC2 bind-mount) support"
```

---

### Task 4: Virtual `/dev/mtd5` Bootlogo Partition & Optional Read-Only `/dev/mtd0` Backing (`MEMGETBADBLOCK` & `MEMERASE`)

**Files:**
- Modify: `tools/arm_hal_shim/hal_gpio_shim.c:330-560`
- Modify: `tools/build_rootfs.sh:368-385`
- Modify: `tools/prebuilt/libhal_gpio_shim.so` (via `tools/build_shims.sh`)
- Modify: `tools/tests/test_emu_plan4_variants_and_persistence.py`

**Interfaces:**
- Consumes: `VDiagProxy` (`0x1fb4dc` / `0x1fb7f0` / `0x1fb9b8`) and `VETMHmiLookFeelPage` (`0x2290f8`) calls to `open("/dev/mtd5", O_RDWR)`, `ioctl(fd, 0x40084d0b, &off)` (`MEMGETBADBLOCK`), `ioctl(fd, 0x40084d02, &erase_info)` (`MEMERASE`), and `pread`/`pwrite` in `0x800`-byte pages at offsets `0x0` and `0x300000`; plus `opt/sysupdate` (`780 0x1be24` `load_pubkey_from_BSP`) reading the HAB RSA-2048 public key from `/dev/mtd0`.
- Produces:
  - Virtual `/dev/mtd5` interception in `tools/arm_hal_shim/hal_gpio_shim.c` backed by `/pss2/mtd5.bin` (auto-created at 12 MB `0xC00000` filled with `0xFF` on first open if not already seeded from a dump's `mtd5.bin`), returning `0` on `MEMGETBADBLOCK` (`0x40084d0b`) and writing `0xFF` over `[start, start+length)` on `MEMERASE` (`0x40084d02`) with strict `start + length <= 0xC00000` bounds checks.
  - Optional read-only `/dev/mtd0` redirection to `/pss2/mtd0.bin` when present (seeded from `mtd/mtd0.bin` in dump mode) so `opt/sysupdate`'s `load_pubkey_from_BSP` (`780`) can read U-Boot's HAB key cleanly.
  - Automatic seeding of `/pss2/mtd5.bin` (and `/pss2/mtd0.bin` if present) in `tools/build_rootfs.sh` when staging from a dump directory.

- [ ] **Step 1: Add failing test for `/dev/mtd5` `MEMGETBADBLOCK` & `MEMERASE` emulation**

Append to `TestEmuPlan4VariantsAndPersistence` in `tools/tests/test_emu_plan4_variants_and_persistence.py`:

```python
    def test_hal_shim_emulates_virtual_mtd5_with_memerase_and_badblock(self):
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        self.assertIn("/dev/mtd5", shim_c)
        self.assertIn("0x40084d0b", shim_c)  # MEMGETBADBLOCK
        self.assertIn("0x40084d02", shim_c)  # MEMERASE
        self.assertIn("0xc00000", shim_c.lower())
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v`
Expected: FAIL (`/dev/mtd5` not handled in `hal_gpio_shim.c`).

- [ ] **Step 3: Implement virtual `/dev/mtd5` in `hal_gpio_shim.c` and dump seeding in `build_rootfs.sh`**

1. In `tools/arm_hal_shim/hal_gpio_shim.c`:
   - Track `static int g_mtd5_fd = -1;` (`MTD5_SIZE = 0x00C00000u`, 12 MB).
   - Add `open_virtual_mtd5(real_open_fn)`: opens `/pss2/mtd5.bin` (`O_RDWR | O_CREAT, 0666`), extends to `0xC00000` bytes filled with `0xFF` if smaller, records `g_mtd5_fd = fd`.
   - Intercept `strcmp(pathname, "/dev/mtd5") == 0` in `open()` and `open64()`, and clear `g_mtd5_fd` in `close()`.
   - In `ioctl()`, when `fd >= 0 && fd == g_mtd5_fd`:
     - `req == 0x40084d0bUL` (`MEMGETBADBLOCK`): validate `*(uint64_t *)arg < 0xC00000ULL` (return `-1`, `errno = EINVAL` if out of range), else return `0` (good block).
     - `req == 0x40084d02UL` (`MEMERASE`): read `struct { uint32_t start; uint32_t length; } *ei = arg;`, reject if `ei->start > 0xC00000u || ei->length > 0xC00000u - ei->start` (`errno = EINVAL; return -1;`), else write `0xFF` bytes across `[ei->start, ei->start + ei->length)` via `pwrite()` in 4 KB chunks and return `0`.
2. In `tools/build_rootfs.sh`, when staging from a dump directory containing `$DUMP_MTD/mtd5.bin`, copy it to `$STAGE_DIR/pss2/mtd5.bin`.
3. Rebuild prebuilt shims: `./tools/build_shims.sh --force --update-prebuilt`.

- [ ] **Step 4: Run tests**

Run: `python3 tools/tests/test_emu_plan4_variants_and_persistence.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_hal_shim/hal_gpio_shim.c tools/build_rootfs.sh tools/prebuilt/libhal_gpio_shim.so tools/tests/test_emu_plan4_variants_and_persistence.py
git commit -m "tools/arm_hal_shim: emulate virtual /dev/mtd5 (MEMGETBADBLOCK, MEMERASE) backed by /pss2/mtd5.bin"
```
