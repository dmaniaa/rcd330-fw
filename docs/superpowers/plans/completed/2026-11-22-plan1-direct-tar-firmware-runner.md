# Direct `.tar` Firmware Runner & Factory Base Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable the RCD 330 emulator (`./tools/run_rcd330.sh` and `./tools/build_rootfs.sh`) to accept any unmodified original Noname/Visteon `.tar` firmware file (such as `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` or `original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar`) or extracted firmware directory, stage a clean Factory Base Layer (`build/factory_base.cpio.gz`) separate from the firmware payload, stub the i.MX6 USB PHY1 MMIO block (`0x020c9000`) used by `5606_skoda/opt/launch.sh`, and drop redundant 512 MB `.ext2` rebuilds by default.

**Architecture:** Noname/Visteon RCD 330 update archives are flat, unencrypted POSIX tarballs containing `kernelImage` (64-byte U-Boot `uImage` header + ARM `zImage`) and the rootfs overlay trees (`opt`, `etc`, `usr`, `lib`, `mirrorlink`, `bt_ac_firm`), omitting only the factory-flashed `/bin/busybox` applets and `/usr/local/qte4.7.4/lib/libQtCore.so.4`. We split rootfs generation into a cached `build/factory_base.cpio.gz` plus a per-firmware cached `<fw_id>_rootfs.cpio.gz` and `<fw_id>_zImage`, and concatenate the two `.cpio.gz` archives (supported natively by the Linux kernel's initramfs unpacker) or merge them into `build/cache/<fw_id>/initramfs.cpio.gz` so switching between `5624_vw` and `5606_skoda` takes a single CLI flag (`--fw <path-to-tar-or-dir>`).

**Tech Stack:** C (QEMU i.MX6 board emulation in `qemu/hw/arm/rcd330.c`), Bash (`tools/build_rootfs.sh`, `tools/run_rcd330.sh`), Python 3 (automated verification tests in `tools/tests/`).

**Spec:** `docs/firmware-packaging-and-update.md`, `docs/decompiled/hmi/diff_5624_vs_5606.md`, `docs/architecture.md`

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- Do NOT re-decompile binaries; consult `docs/decompiled/` and `docs/*.md`.
- Maintain backward compatibility: running `./tools/build_rootfs.sh` or `./tools/run_rcd330.sh` with no arguments must still default to `5624_vw` (preferring `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` if given via `--fw`, or `extracted-fw/5624_vw` / `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` automatically) and produce/update `build/rootfs_5624.cpio.gz` and `build/5624_zImage` symlinks or files.
- `5606_skoda/opt/launch.sh` executes `./memtool 0x20c9018=0xffffffff` and `./memtool 0x20c9014=0x10040405`; `qemu/hw/arm/rcd330.c` must map the i.MX6 USB PHY1/2 stub at `0x020c9000..0x020cbfff` (`0x3000` bytes) so register reads/writes succeed cleanly.
- Skip building the 512 MB `.ext2` image in `tools/build_rootfs.sh` unless `--with-ext2` is explicitly passed, cutting build time and disk usage.

## Review Focus

- Passing `--fw original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar` vs `--fw original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` vs `--fw extracted-fw/5624_vw` must all extract `kernelImage` (stripping the 64-byte U-Boot `uImage` magic `0x27051956` header) and stage the rootfs cpio archive without mutating `original-fw/` or `extracted-fw/`.
- If an input `kernelImage` is already a raw `zImage` (does not start with U-Boot `uImage` big-endian magic `27 05 19 56`), it must be copied as-is without stripping 64 bytes.
- Tarballs with non-writable directory permissions (e.g. `vipImage.bin` mode `0600` in `5606_skoda`) must be staged with `chmod -R u+rwX` so repeated builds never fail with `Permission denied`.
- `tools/run_rcd330.sh --dry-run --fw <tar>` must validate and print the resolved kernel, initramfs, and QEMU command line without launching the GUI or blocking on `qemu-system-arm`.

---

### Task 1: Map i.MX6 USB PHY1/PHY2 Stub (`0x020c9000`) in `qemu/hw/arm/rcd330.c`

**Files:**
- Modify: `qemu/hw/arm/rcd330.c:258-268`
- Test: `tools/tests/test_plan1_tar_runner.py`

**Interfaces:**
- Consumes: `MxsStubState` and `mxs_stub_ops` already defined in `qemu/hw/arm/rcd330.c:28-82` (which implement i.MX `CTRL` `+0x0`, `SET` `+0x4`, `CLR` `+0x8`, `TOG` `+0xc` register semantics used by MXS/i.MX6 blocks including USBPHY at `0x020c9000` and `0x020ca000`).
- Produces: Mapped `"rcd330-usbphy-stub"` `MxsStubState` region at `0x020c9000` (size `0x2000`) in `qemu/hw/arm/rcd330.c` so `5606_skoda/opt/launch.sh` (`./memtool 0x20c9018=0xffffffff` and `0x20c9014=0x10040405`) executes without unassigned-memory faults.

- [ ] **Step 1: Write the failing test in `tools/tests/test_plan1_tar_runner.py`**

```python
#!/usr/bin/env python3
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan1TarRunner(unittest.TestCase):
    def test_rcd330_c_maps_usbphy_stub(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        self.assertIn("0x020c9000", rcd330_c)
        self.assertIn("rcd330-usbphy-stub", rcd330_c)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: FAIL (`"0x020c9000" not found in rcd330_c`)

- [ ] **Step 3: Add the `rcd330-usbphy-stub` region in `qemu/hw/arm/rcd330.c` and rebuild QEMU**

In `qemu/hw/arm/rcd330.c`, immediately after mapping `rcd330-mxs-stub` at `0x00112000`, add:

```c
    /*
     * Map i.MX6 USBPHY1/2 register block (0x020c9000..0x020cafff) with
     * CTRL/SET/CLR/TOG semantics so 5606_skoda/opt/launch.sh memtool writes
     * (0x20c9018=0xffffffff, 0x20c9014=0x10040405) succeed cleanly.
     */
    MxsStubState *usbphy_stub = g_new0(MxsStubState, 1);
    memory_region_init_io(&usbphy_stub->iomem, OBJECT(machine), &mxs_stub_ops,
                          usbphy_stub, "rcd330-usbphy-stub", 0x2000);
    memory_region_add_subregion(get_system_memory(), 0x020c9000,
                                &usbphy_stub->iomem);
```

Then run `./tools/build_qemu.sh` to compile `qemu/build/qemu-system-arm`.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/arm/rcd330.c
git -C qemu commit -m "hw/arm/rcd330: map i.MX6 USBPHY1/2 register block at 0x020c9000 for 5606_skoda"
```

---

### Task 2: Refactor `tools/build_rootfs.sh` and `tools/run_rcd330.sh` to Accept Any `.tar` Firmware or Extracted Directory

**Files:**
- Modify: `tools/build_rootfs.sh`
- Modify: `tools/run_rcd330.sh`
- Modify: `tools/tests/test_plan1_tar_runner.py`

**Interfaces:**
- Consumes: `original-fw/*.tar` or `extracted-fw/<dir>`, `tools/build_shims.sh`, `tools/prebuilt/busybox-armv5l`, `tools/prebuilt/libQtCore.so.4`.
- Produces:
  - `tools/build_rootfs.sh [--fw <path.tar|dir>] [--with-ext2] [--out-cpio <path>] [--out-zimage <path>]`
  - `build/factory_base.cpio.gz`: Reusable Factory Base Layer containing `/bin/busybox` applets, `/sbin/*` applets, `/usr/local/qte4.7.4/lib/libQtCore.so.4`, and guest shims.
  - `build/cache/<fw_id>/zImage` and `build/cache/<fw_id>/rootfs.cpio.gz` (plus default symlinks `build/5624_zImage` and `build/rootfs_5624.cpio.gz` when building `5624`).
  - `tools/run_rcd330.sh [--fw <path.tar|dir>] [--dry-run] [--rebuild]` that auto-stages any `.tar` or directory and boots QEMU without requiring a 512 MB `.ext2` drive unless present or requested.

- [ ] **Step 1: Add tests for `.tar` extraction, `uImage` magic check, `factory_base.cpio.gz`, and `--dry-run` to `tools/tests/test_plan1_tar_runner.py`**

Update `tools/tests/test_plan1_tar_runner.py`:

```python
#!/usr/bin/env python3
import gzip
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan1TarRunner(unittest.TestCase):
    def test_rcd330_c_maps_usbphy_stub(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        self.assertIn("0x020c9000", rcd330_c)
        self.assertIn("rcd330-usbphy-stub", rcd330_c)

    def test_build_rootfs_from_5606_tar(self):
        tar_5606 = ROOT / "original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar"
        if not tar_5606.exists():
            self.skipTest("5606 tar not present")
        res = subprocess.run(
            [str(ROOT / "tools/build_rootfs.sh"), "--fw", str(tar_5606)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertTrue((ROOT / "build/factory_base.cpio.gz").exists())
        zimg = ROOT / "build/cache/std_plus_pss-6.5inch_5606_SKODA_DE_CV/zImage"
        cpio = ROOT / "build/cache/std_plus_pss-6.5inch_5606_SKODA_DE_CV/rootfs.cpio.gz"
        self.assertTrue(zimg.exists(), res.stdout + res.stderr)
        self.assertTrue(cpio.exists(), res.stdout + res.stderr)
        # Verify zImage has ARM Linux zImage magic 0x016f2818 at offset 0x24
        hdr = zimg.read_bytes()[:0x30]
        self.assertEqual(hdr[0x24:0x28], b"\x18\x28\x6f\x01")

    def test_run_rcd330_dry_run_switches_firmware(self):
        tar_5624 = ROOT / "original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar"
        fw_arg = str(tar_5624) if tar_5624.exists() else str(ROOT / "extracted-fw/5624_vw")
        res = subprocess.run(
            [str(ROOT / "tools/run_rcd330.sh"), "--dry-run", "--fw", fw_arg],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn("-M rcd330", res.stdout)
        self.assertIn("zImage", res.stdout)
        self.assertIn("rootfs", res.stdout)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: FAIL (`build_rootfs.sh` does not yet accept `--fw` or generate `build/factory_base.cpio.gz`).

- [ ] **Step 3: Update `tools/build_rootfs.sh` and `tools/run_rcd330.sh`**

Update `tools/build_rootfs.sh` so that:
1. It parses `--fw <tar_or_dir>`, `--with-ext2`, `--force`, `--out-cpio <path>`, and `--out-zimage <path>`. If `--fw` is omitted, it defaults to `extracted-fw/5624_vw` (or `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` if `extracted-fw/5624_vw` does not exist).
2. It builds `build/factory_base.cpio.gz` from `build/factory_base_stage` (rebuilt only when missing, `--force`, or `tools/build_shims.sh` outputs are newer) containing:
   - `/bin/busybox` and `/bin/*`, `/sbin/*`, `/linuxrc` applets
   - `/usr/local/qte4.7.4/lib/libQtCore.so.4` and `/usr/lib/libQtCore.so.4`
   - Guest shims (`/usr/lib/libEGL.so.1`, `/usr/lib/libGLESv2.so.2`, `/opt/lib/libhal_gpio_shim.so`, `/sbin/init`)
3. For the target firmware (`--fw`), it computes `FW_ID` (`5624_vw` for default/5624 extracted dir, or `$(basename "$FW_SRC" .tar)`):
   - Stages into `build/cache/$FW_ID/stage`: if `$FW_SRC` is a `.tar` file, extracts `kernelImage`, `etc`, `lib`, `usr`, `opt`, `var`, `mirrorlink`, `bt_ac_firm` directly via `tar -xf "$FW_SRC" -C "$STAGE_DIR"` and runs `chmod -R u+rwX "$STAGE_DIR"`; if `$FW_SRC` is a directory, copies those subdirectories and `kernelImage`.
   - Extracts `build/cache/$FW_ID/zImage` from `kernelImage`: checks the first 4 bytes for U-Boot `uImage` magic `27051956` (`head -c 4 | od -An -tx1 | tr -d ' \n'`); if `27051956`, strips 64 bytes (`dd if=... of=... bs=64 skip=1 status=none`), else copies verbatim.
   - Merges `build/factory_base_stage/` onto `STAGE_DIR`, populates `/pss1` from `/opt/{stdplus.ini,config,hmi_autotest.ini}`, writes `/etc/rc.d/rc.qemu`, `/etc/inittab`, `/etc/profile`, and generates `build/cache/$FW_ID/rootfs.cpio.gz`.
   - When `FW_ID` contains `5624`, also updates `build/5624_zImage` and `build/rootfs_5624.cpio.gz` (and `build/rootfs_5624.ext2` only when `--with-ext2` is passed).

Update `tools/run_rcd330.sh` so that:
1. It accepts `--fw <tar_or_dir>`, `--dry-run`, `--rebuild`, and `--sd <image>`.
2. It resolves `FW_ID`, invokes `tools/build_rootfs.sh --fw "$FW_SRC"` if the cached `zImage` or `rootfs.cpio.gz` is missing (or `--rebuild` is set).
3. If `--dry-run` is passed, it prints the resolved `FW_ID`, `ZIMAGE`, `ROOTFS`, and the exact `qemu-system-arm` command line and exits `0`.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: PASS (3/3 tests passing).

- [ ] **Step 5: Commit**

```bash
git add tools/build_rootfs.sh tools/run_rcd330.sh tools/tests/test_plan1_tar_runner.py
git commit -m "tools: support direct .tar firmware execution and factory base layer caching"
```
