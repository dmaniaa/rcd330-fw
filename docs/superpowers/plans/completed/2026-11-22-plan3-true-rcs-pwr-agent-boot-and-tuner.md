# True `/etc/rc.d/rcS` -> `/opt/launch.sh` -> `opt/pwr_agent` Supervision Chain & VIP AM/FM Tuner Simulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot the emulated system through the firmware's unmodified `/etc/inittab` -> `/etc/rc.d/rcS` -> `/opt/launch.sh` -> `opt/pwr_agent` supervision chain (eliminating custom `/sbin/init`, `/etc/rc.d/rc.qemu`, and `vmf_send` wake shell hacks), bring up all supervised services (`opt/vgw`, `opt/pss`, `opt/audio_proc`, `opt/media`, `opt/bt`, `opt/hmi`, `opt/diag`), and implement full AM/FM + RDS station simulation in `rcd330-vip`.

**Architecture:** On real hardware, BusyBox `/sbin/init` reads `/etc/inittab` (`::sysinit:/etc/rc.d/rcS`). `rcS` mounts `/dev`, `/sys`, `/proc`, `/tmp`, runs `/etc/rc.d/init.d/mdev`, attaches `/pss1` and `/pss2` via `pss_handle_with_recovery`, seeds `/pss1` on first boot, and executes `/opt/launch.sh`, which starts `vmf`, `hal_init`, `pwr_agent -o -v`, `sysupdate`, and `mfid`. `opt/pwr_agent` (`FUN_0000d59c`) then spawns `vgw`, `pss`, `media`, `audio_proc`, `bt`, `hmi`, `diag`, etc. in staged power groups once VIP completes its `0x01:0x01` / `0x01:0x03` handshake. By providing lightweight `/sbin/ubiattach`, `/sbin/ubidetach`, `/sbin/ubiformat`, `/sbin/ubimkvol` shims and making `/bin/mount` succeed for `ubi1:pss1` / `ubi2:pss2`, the unmodified `rcS` and `launch.sh` run verbatim. In `qemu/hw/misc/rcd330_vip.c`, we add periodic/reactive power-state & AM/FM tuner (`IPCL 0x04:0x01 -> 0x05:0x00`) simulation so `pwr_agent` transitions all groups to `RUN` automatically and the Radio screen displays live FM/AM frequencies, station presets, and RDS PS/RadioText.

**Tech Stack:** C (`qemu/hw/misc/rcd330_vip.c`), Bash (`tools/build_rootfs.sh`, `tools/build_shims.sh`), Python 3 (`tools/rcd330_panel.py`, `tools/tests/test_plan3_rcs_boot_and_tuner.py`).

**Spec:** `docs/architecture.md`, `docs/ipc-and-vgw.md`, `docs/hmi-internals.md`, `docs/decompiled/services/pwr_agent/functions/0xd59c.c`, `docs/decompiled/hmi/functions/0xae28c.c`

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- Do NOT overwrite `/etc/rc.d/rcS` or `/opt/launch.sh` in the staged rootfs — they must remain identical to the original files in the firmware `.tar`.
- `etc/inittab` in the firmware runs `::sysinit:/etc/rc.d/rcS`. To also provide an interactive serial console on `ttymxc0` (UART1 in QEMU) without losing the original `::sysinit:/etc/rc.d/rcS`, `/sbin/init` must be the standard BusyBox `init` symlink (`../bin/busybox`), not a custom binary that calls `rc.qemu`.
- In `qemu/hw/misc/rcd330_vip.c`, when `opt/vgw` connects and sends any frame or when `rcd330-vip` detects `vgw` polling SPI / MMIO, `rcd330-vip` must send the VIP startup handshake (`IPCL 0x01:0x01` `[0x01]`, `IPCL 0x01:0x01` `[0x03, 0x03, 0x00, 0x01]`, `IPCL 0x01:0x03` `[0x05, 0x05, 0x00, 0x00, 0x00]`) and audio mode grant (`IPCL 0x08:0x02` `[0x81, 0x03, 0x03, 0x01]`) plus initial FM tuner status (`IPCL 0x05:0x00`), so no `vmf_send` shell loop is needed.

## Review Focus

- In `etc/rc.d/rcS`, `pss_handle_with_recovery()` runs:
  - `ubiattach /dev/ubi_ctrl -d 1 -m 3`
  - `ubiattach /dev/ubi_ctrl -d 2 -m 4`
  - `mount -t ubifs -o sync ubi1:pss1 /pss1/`
  - `mount -t ubifs -o sync ubi2:pss2 /pss2/`
  And then line 308 runs `mount -o remount,ro /`.
  If `/sbin/ubiattach` returns `0` and `/bin/mount` intercepts `-t ubifs` (returning `0` since `/pss1` and `/pss2` already exist in initramfs) and ignores `remount,ro /` (so `/tmp` and `/sysupdate` remain writable in initramfs), `rcS` proceeds directly to `cd /opt && ./launch.sh` with zero errors!
- In `opt/launch.sh`, `insmod /mirrorlink/usbnet.ko`, `insmod /mirrorlink/cdc_ncm.ko`, and `insmod g_iap_ncm_audio.ko` are executed, and `./pwr_agent -o -v &` spawns the remaining services via `system()` (which inherits `LD_PRELOAD=/opt/lib/libhal_gpio_shim.so` if exported in `/etc/profile` or `LD_PRELOAD` environment before `rcS` / `launch.sh`).
- In `VAMFMProxy::RecieveVmfMsg` (`0xae28c.c`), `IPCL(0x05, 0x00)` maps to `VMF(0x6F, 0x00)`:
  - Sub-command `0x3d` (15 bytes): `[0x3d, freq_lo, freq_hi, ps0..ps7, band, flags, state]` — wait: let's check endianness of `freq` at `0xadf18.c`:
    In `0xadf18.c` (`OnSetFrequencyOpera`):
    `local_18` byte 0 is `0x65`, byte 1 (`pData[5]`) is `*(uint16_t*)(param_1 + 0x38) >> 8` (high byte), byte 2 (`pData[6]`) is `*(uint16_t*)(param_1 + 0x38) & 0xff` (low byte)! And when `0x3d` is received (`0xae28c.c`), `memcpy(param_1 + 8, param_4 + 1, 0xe)` copies `param_4[1..2]` directly into the 16-bit little-endian `uint16_t` at `param_1 + 8`! Let's support both big-endian TX in `0x04:0x01` (`0x65, freq_hi, freq_lo`) and little-endian/big-endian RX in `0x05:0x00`!

---

### Task 1: Implement AM/FM + RDS Tuner State Machine & Auto-Wake Handshake in `qemu/hw/misc/rcd330_vip.c`

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c`
- Modify: `tools/rcd330_panel.py`
- Test: `tools/tests/test_plan3_rcs_boot_and_tuner.py`

**Interfaces:**
- Consumes: `vip_enqueue_ipcl` and `vip_handle_host_frame` in `qemu/hw/misc/rcd330_vip.c`.
- Produces:
  - Simulated AM/FM tuner in `Rcd330VipState`:
    - 6 FM presets (`8750` = 87.5 MHz `"VW RADIO"`, `9120` = 91.2 MHz `"BAYERN 3"`, `9580` = 95.8 MHz `"RCD 330 "`, `9850` = 98.5 MHz `"SWR3    "`, `10140` = 101.4 MHz `"RADIO 1 "`, `10460` = 104.6 MHz `"CLASSIC "`) and AM frequency (`999` kHz `"AM INFO "`).
    - Handles host TX `IPCL(0x04, 0x01)` (routed from `VMF 0x8F:0x22` by `opt/vgw`):
      - `payload[0] == 0x65`: Set frequency `(payload[1] << 8) | payload[2]`
      - `payload[0] == 0x37`: Seek/step frequency up (`payload[2] == 1`) or down (`payload[2] == 2`)
      - Any other `0x04:0x01` command (band switch / status query / seek mode): replies with current `0x3d` (frequency, 8-char RDS PS, band, stereo/RDS flags, state `9`), `0x3f` (72-char RDS RadioText), and `0x40` (station list).
    - Also responds to `IPCL(0x01, 0x03)` watchdog keepalives (`pwr_agent -> VIP` every ~1 s) by auto-sending the initial `0x01:0x01` RUN state, `0x01:0x03` DEV_STS, `0x08:0x02` audio mode, and `0x05:0x00` tuner status on the first 3 keepalives so `pwr_agent` and `hmi` always reach full RUN state without `vmf_send` shell hacks!
    - Control socket command `TUNER <freq_khz_or_10khz> [PS_NAME]` (and UI controls in `tools/rcd330_panel.py`) to change radio station and RDS PS text live from the browser panel.

- [ ] **Step 1: Write the failing test in `tools/tests/test_plan3_rcs_boot_and_tuner.py`**

```python
#!/usr/bin/env python3
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan3RcsBootAndTuner(unittest.TestCase):
    def test_vip_implements_amfm_tuner_and_watchdog_handshake(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        self.assertIn("vip_send_tuner_status", vip_c)
        self.assertIn("grp == 0x04 && sub == 0x01", vip_c)
        self.assertIn("TUNER", vip_c)

    def test_panel_exposes_tuner_controls(self):
        panel_py = (ROOT / "tools/rcd330_panel.py").read_text()
        self.assertIn("TUNER", panel_py)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan3_rcs_boot_and_tuner.py -v`
Expected: FAIL (`vip_send_tuner_status` not found in `rcd330_vip.c`).

- [ ] **Step 3: Implement `vip_send_tuner_status`, `IPCL(0x04, 0x01)` handler, watchdog auto-handshake, and `TUNER` command in `qemu/hw/misc/rcd330_vip.c` and `tools/rcd330_panel.py`**

In `qemu/hw/misc/rcd330_vip.c`:
1. Add tuner fields to `Rcd330VipState`:
```c
    uint16_t fm_freq;    /* in 10 kHz units, e.g. 9850 = 98.50 MHz */
    uint8_t tuner_band;  /* 0=FM1, 1=FM2, 3=AM */
    char rds_ps[9];      /* 8-char RDS PS name */
    char rds_rt[73];     /* 72-char RDS RadioText */
    uint8_t wd_handshakes_sent;
```
2. Implement `vip_send_tuner_status(Rcd330VipState *s)`:
```c
static void vip_send_tuner_status(Rcd330VipState *s)
{
    /*
     * IPCL(0x05, 0x00) -> VMF(0x6f, 0x00) VAMFMProxy::RecieveVmfMsg (0xae28c):
     *   0x3d: [0x3d, freq_lo, freq_hi, ps[0..7], band, flags, state] (15 bytes)
     *   0x3f: [0x3f, rt[0..71]] (73 bytes)
     */
    uint8_t st[15];
    memset(st, 0, sizeof(st));
    st[0] = 0x3d;
    st[1] = (uint8_t)(s->fm_freq & 0xff);
    st[2] = (uint8_t)((s->fm_freq >> 8) & 0xff);
    memset(&st[3], ' ', 8);
    size_t pslen = strlen(s->rds_ps);
    if (pslen > 8) pslen = 8;
    memcpy(&st[3], s->rds_ps, pslen);
    st[11] = s->tuner_band;
    st[12] = 0x03; /* Stereo + RDS available */
    st[13] = 0x09; /* Tuned / Idle state */
    vip_enqueue_ipcl(s, 0x05, 0x00, st, sizeof(st));

    uint8_t rt[73];
    memset(rt, ' ', sizeof(rt));
    rt[0] = 0x3f;
    size_t rtlen = strlen(s->rds_rt);
    if (rtlen > 72) rtlen = 72;
    memcpy(&rt[1], s->rds_rt, rtlen);
    vip_enqueue_ipcl(s, 0x05, 0x00, rt, sizeof(rt));
}
```
3. In `vip_handle_host_frame`:
   - On `grp == 0x01 && sub == 0x03` (watchdog keepalive from `pwr_agent`): if `s->wd_handshakes_sent < 3`, increment `s->wd_handshakes_sent` and send `ready_reply`, `run_mode`, `dev_sts`, `audio_mode_reply` (`0x08:0x02`), and `vip_send_tuner_status(s)`.
   - On `grp == 0x04 && sub == 0x01` (`VAMFMProxy` tuner command, payload at `buf + off + 4`, `plen = mlen - 4`):
     - If `plen >= 3 && pl[0] == 0x65`: set `s->fm_freq = ((uint16_t)pl[1] << 8) | pl[2];` (if `s->fm_freq < 500`, decode as little-endian) and call `vip_send_tuner_status(s)`.
     - If `plen >= 3 && pl[0] == 0x37`: step `s->fm_freq` by `+20` (`+0.2 MHz`) when `pl[2] == 1` or `-20` when `pl[2] == 2` (wrapping inside `8750..10800`), update `s->rds_ps` from a preset table, and call `vip_send_tuner_status(s)`.
     - Otherwise: call `vip_send_tuner_status(s)`.
4. In `vip_process_cmd`, handle `TUNER <freq> [PS]`: update `s->fm_freq`, `s->rds_ps`, `s->rds_rt`, call `vip_send_tuner_status(s)`, and reply `"OK TUNER\n"`.
5. In `tools/rcd330_panel.py`, add an FM Radio Station / RDS preset bar (`87.5 VW RADIO`, `95.8 RCD 330`, `98.5 SWR3`, `104.6 CLASSIC`) that sends `TUNER <freq> <ps>`.

- [ ] **Step 4: Build QEMU and run the test**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_plan3_rcs_boot_and_tuner.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: add AM/FM RDS tuner simulation and watchdog auto-handshake"
git add tools/rcd330_panel.py tools/tests/test_plan3_rcs_boot_and_tuner.py
git commit -m "tools/rcd330_panel: add FM/RDS station controls to web faceplate"
```

---

### Task 2: Boot Through Unmodified `/etc/rc.d/rcS` -> `/opt/launch.sh` -> `opt/pwr_agent`

**Files:**
- Modify: `tools/build_rootfs.sh`
- Modify: `tools/arm_init/init.c`
- Modify: `tools/tests/test_plan3_rcs_boot_and_tuner.py`

**Interfaces:**
- Consumes: Original `/etc/rc.d/rcS`, `/opt/launch.sh`, and `/etc/inittab` from the firmware tarball, plus `opt/pwr_agent` (`FUN_0000d59c`).
- Produces:
  - In `tools/build_rootfs.sh`:
    - No `/etc/rc.d/rc.qemu` script and no `vmf_send` wake loop!
    - Lightweight UBI/mount wrappers in `/sbin/ubiattach`, `/sbin/ubidetach`, `/sbin/ubiformat`, `/sbin/ubimkvol` (returning `0`) and a `/bin/mount` wrapper that delegates to `/bin/busybox mount` except when mounting `ubi*` (succeeds as no-op because `/pss1` and `/pss2` are already populated in initramfs) or `remount,ro /` (succeeds as no-op so `/tmp` and `/sysupdate` stay writable in initramfs).
    - `/etc/rc.d/rc.local` or `/etc/ld.so.preload` (`/opt/lib/libhal_gpio_shim.so`) so every service spawned by `/etc/rc.d/rcS`, `/opt/launch.sh`, and `opt/pwr_agent` automatically loads `/opt/lib/libhal_gpio_shim.so` and sees `/dev/spidev0.0`, `/dev/v850srq`, `/dev/event1`, and `/dev/tuner_knob` without modifying `/etc/rc.d/rcS` or `/opt/launch.sh`!
  - In `tools/arm_init/init.c`: Executes `/etc/rc.d/rcS` directly (the firmware's real sysinit script) instead of `/etc/rc.d/rc.qemu`.

- [ ] **Step 1: Add test verifying `rc.qemu` and `vmf_send` hacks are gone and unmodified `rcS` + `launch.sh` are used**

Append to `TestPlan3RcsBootAndTuner` in `tools/tests/test_plan3_rcs_boot_and_tuner.py`:

```python
    def test_rootfs_uses_unmodified_rcS_and_launch_sh(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        init_c = (ROOT / "tools/arm_init/init.c").read_text()
        self.assertNotIn("rc.qemu", build_sh)
        self.assertNotIn("vmf_send 16 1 1", build_sh)
        self.assertIn("/etc/rc.d/rcS", init_c)
        self.assertNotIn("rc.qemu", init_c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan3_rcs_boot_and_tuner.py -v`
Expected: FAIL (`rc.qemu` still present in `build_rootfs.sh` and `init.c`).

- [ ] **Step 3: Update `tools/arm_init/init.c` and `tools/build_rootfs.sh` to boot unmodified `/etc/rc.d/rcS` -> `/opt/launch.sh`**

1. In `tools/arm_init/init.c`:
   - Pre-create `/dev/console`, `/dev/null`, `/dev/zero`, `/dev/mem`, `/dev/tty`, `/dev/fb0`, `/dev/fb1`, `/dev/i2c-0..3`, `/dev/spidev0.0` (`c 153 0`), `/dev/tuner_knob` (`c 265 0`), `/dev/v850srq` (`c 200 0`), `/dev/event1` (`c 13 65` -> `/dev/input/event1`), `/dev/input/event0` (`c 13 64`), `/dev/input/event1` (`c 13 65`).
   - Set `setenv("LD_PRELOAD", "/opt/lib/libhal_gpio_shim.so", 1);` before forking `/bin/sh /etc/rc.d/rcS` so `rcS`, `launch.sh`, and `pwr_agent` (and all children spawned by `pwr_agent` via `system()`) inherit `LD_PRELOAD` automatically without editing `rcS` or `launch.sh`!
2. In `tools/build_rootfs.sh`:
   - Remove the `cat > "$STAGE_DIR/etc/rc.d/rc.qemu"` block and the `vmf_send` wake loop.
   - Install tiny `/sbin/ubiattach`, `/sbin/ubidetach`, `/sbin/ubiformat`, `/sbin/ubimkvol` shell scripts (`#!/bin/sh\nexit 0`) in `factory_base_stage/sbin/` so `pss_handle_with_recovery` in unmodified `/etc/rc.d/rcS` succeeds.
   - Install a `/bin/mount` wrapper in `factory_base_stage/bin/mount` that returns `0` when the arguments contain `ubifs` or `remount,ro` and otherwise execs `/bin/busybox mount "$@"`.
   - Rebuild shims (`./tools/build_shims.sh --force --update-prebuilt`) and rebuild rootfs (`./tools/build_rootfs.sh --force`).

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 tools/tests/test_plan3_rcs_boot_and_tuner.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_init/init.c tools/build_rootfs.sh tools/prebuilt/ tools/tests/test_plan3_rcs_boot_and_tuner.py
git commit -m "tools: boot unmodified /etc/rc.d/rcS -> /opt/launch.sh -> opt/pwr_agent chain"
```

---

### Task 3: Remove Legacy Launch & Rootfs Paths (`extracted-fw` Directory Staging, `rootfs_5624.*`, and `.ext2` Generation)

**Files:**
- Modify: `tools/run_rcd330.sh`
- Modify: `tools/build_rootfs.sh`
- Modify: `tools/tests/test_plan1_tar_runner.py`
- Modify: `tools/tests/test_plan3_rcs_boot_and_tuner.py`

**Interfaces:**
- Consumes: `.tar` firmware archives in `original-fw/*.tar` (or any path to a `.tar` firmware file passed as a positional argument `./tools/run_rcd330.sh <firmware.tar>` or `--fw <firmware.tar>`).
- Produces:
  - `tools/run_rcd330.sh [<firmware.tar>] [--fw <firmware.tar>] [--dry-run] [--rebuild] [--sd <sd_img>]`:
    - Accepts `<firmware.tar>` directly as the first positional argument or via `--fw`.
    - Defaults to `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` (or the first `original-fw/*.tar` found).
    - Rejects non-`.tar` / legacy directory inputs with a clear error message.
  - `tools/build_rootfs.sh [<firmware.tar>] [--fw <firmware.tar>] [--force]`:
    - Only accepts `.tar` firmware files (removes `elif [[ -d "$FW_SRC" ]]` legacy `extracted-fw/` directory copying).
    - Removes `--with-ext2`, `MKE2FS`, `rootfs_5624.ext2`, `build/5624_zImage`, and `build/rootfs_5624.cpio.gz` legacy outputs; outputs exclusively to `build/factory_base.cpio.gz` and `build/cache/<fw_id>/{zImage,rootfs.cpio.gz}`.
    - Cleans up any leftover `build/5624_zImage`, `build/rootfs_5624.cpio.gz`, `build/rootfs_5624.ext2`, and `build/rootfs_stage` artifacts.

- [ ] **Step 1: Add tests asserting legacy `extracted-fw`, `rootfs_5624`, and `--with-ext2` paths are removed and positional `<firmware.tar>` works**

Append to `TestPlan3RcsBootAndTuner` in `tools/tests/test_plan3_rcs_boot_and_tuner.py`:

```python
    def test_legacy_launch_paths_removed_and_positional_tar_works(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertNotIn("extracted-fw", build_sh)
        self.assertNotIn("extracted-fw", run_sh)
        self.assertNotIn("rootfs_5624", build_sh)
        self.assertNotIn("rootfs_5624", run_sh)
        self.assertNotIn("--with-ext2", build_sh)
        self.assertNotIn("mke2fs", build_sh)
        tar_5606 = ROOT / "original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar"
        if tar_5606.exists():
            res = subprocess.run(
                [str(ROOT / "tools/run_rcd330.sh"), str(tar_5606), "--dry-run"],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn("std_plus_pss-6.5inch_5606_SKODA_DE_CV", res.stdout)
```

Also update `tools/tests/test_plan1_tar_runner.py` so `test_run_rcd330_dry_run_switches_firmware` passes `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` directly without referencing `extracted-fw/5624_vw`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan3_rcs_boot_and_tuner.py -v`
Expected: FAIL (`extracted-fw` and `rootfs_5624` still present in `build_rootfs.sh` and `run_rcd330.sh`).

- [ ] **Step 3: Remove legacy paths from `tools/build_rootfs.sh` and `tools/run_rcd330.sh`**

1. In `tools/build_rootfs.sh`:
   - Accept `<firmware.tar>` as either positional `$1` or `--fw <firmware.tar>`, defaulting to `$ROOT_DIR/original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar`.
   - Require `[[ -f "$FW_SRC" && "$FW_SRC" == *.tar ]]`.
   - Remove all references to `extracted-fw`, `--with-ext2`, `MKE2FS`, `build/5624_zImage`, `build/rootfs_5624.cpio.gz`, and `build/rootfs_5624.ext2`.
   - Remove stale `build/5624_zImage`, `build/rootfs_5624.cpio.gz`, `build/rootfs_5624.ext2`, and `build/rootfs_stage` if present.
2. In `tools/run_rcd330.sh`:
   - Accept `<firmware.tar>` as positional argument or `--fw <firmware.tar>`, defaulting to `$ROOT_DIR/original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar`.
   - Remove all references to `extracted-fw` and `rootfs_5624`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 tools/tests/test_plan3_rcs_boot_and_tuner.py -v && python3 tools/tests/test_plan1_tar_runner.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/build_rootfs.sh tools/run_rcd330.sh tools/tests/test_plan1_tar_runner.py tools/tests/test_plan3_rcs_boot_and_tuner.py
git commit -m "tools: remove legacy extracted-fw and rootfs_5624 launch paths in favor of direct .tar runner"
```
