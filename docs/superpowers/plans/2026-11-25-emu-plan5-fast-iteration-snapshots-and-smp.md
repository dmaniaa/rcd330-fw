# Emulator Plan 5: Fast HMI Iteration (Instant Overlay CPIO, UART2 Hot-Reload, VM Snapshots & SMP)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the `src/hmi` build-and-test turnaround in QEMU from several minutes down to seconds via instant concatenated overlay initramfs, a UART2 (`ttymxc1`) non-interactive command/hot-reload channel, QEMU VM snapshot save/restore (`savevm`/`loadvm`), and optional dual-core i.MX6DL `-smp 2` execution.

**Architecture:** Today every `--hmi` edit re-archives and re-gzips all 3,666 files in `$STAGE_DIR` (~90 MB), every test cold-boots Linux 3.0.35 from scratch on `-smp 1` (`BOOT_TIMEOUT_S = 240`), and there is no out-of-band guest command channel separate from the noisy `ttymxc0` console. Because Linux's `init/initramfs.c` unpacks concatenated `.cpio.gz` streams in order and unlinks/overwrites existing regular files on collision, we can produce `rootfs_hmi.cpio.gz` in ~50 ms by concatenating the cached stock `rootfs.cpio.gz` with a tiny overlay `.cpio.gz`. Next we wire UART2 (`s->soc.uart[1]` → `/dev/ttymxc1`) to a minimal command/exec shell in `/sbin/init` so `tools/hmi_qemu.py` can run `vmf_send`/`vmf_rcv` or hot-swap `/opt/hmi` without rebooting. We add VM snapshot support via a small dedicated qcow2 state drive (`--snapshot-img`) so Phase 3 parity tests can restore a booted VM in ~1–2 s. Finally, since both `5624` and `5606` kernels are built `SMP PREEMPT` and `fsl-imx6.c` already maps `TYPE_IMX6_SRC`, we audit and enable `--smp 2` so `igdtTask` software GL rendering runs concurrently with `VDisplayTask`/`vgw`/`vmf`.

**Tech Stack:** Bash (`tools/build_rootfs.sh`, `tools/run_rcd330.sh`), C (`tools/arm_init/init.c`, `qemu/hw/arm/rcd330.c`, `qemu/hw/misc/rcd330_vip.c`, `qemu/hw/display/imx6_ipu.c`), Python 3 (`tools/hmi_qemu.py`, `tools/tests/test_emu_plan5_velocity.py`).

**Spec:** `docs/architecture.md`, `tools/build_rootfs.sh`, `tools/run_rcd330.sh`, `tools/hmi_qemu.py`, `qemu/hw/arm/rcd330.c`, `qemu/hw/arm/fsl-imx6.c`.

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- Keep the stock `build/cache/<fw_id>/rootfs.cpio.gz` strictly untouched when building variant overlays.
- Concatenated initramfs overlay MUST be empirically verified inside QEMU Linux 3.0.35 (checking `/opt/hmi` SHA-256 and `/etc/profile` contents inside the running guest) before removing the slow full-rearchive path.
- Default `--smp` stays `1` until all slow QEMU tests (`test_hmi_boot`, `test_hmi_parity`, `test_hmi_interactive`) pass cleanly under `--smp 2` with zero race or GPT regressions.
- VM snapshots (`savevm`/`loadvm`) require VMState descriptors on all custom devices (`rcd330-vip`, `imx6.ipu`, `atmel.mxt`, `rcd330-adv7182`) so restored VMs never lose IPU framebuffer registers, touch calibration state, or VIP EEPROM/power state.

## Review Focus

- **Linux 3.0.35 `initramfs.c` duplicate regular-file overwrite**: In Linux 3.0.35, `do_name()` calls `sys_unlink(collected)` when unpacking a regular file over an existing regular file, **except** if the existing path is a directory or a read-only mode blocks unlink; the overlay archive must contain only regular files (no directory entries) and must verify overwrite in a real QEMU boot test; pinned in Task 1's test.
- **UART2 (`ttymxc1`) command framing & output isolation**: A guest command run over `ttymxc1` may print arbitrary stdout/stderr or exit non-zero; framing each command with a unique sentinel line (`__RCD330_RC__:<rc>:<nonce>`) prevents prompt/output collisions and returns the exact exit code; pinned in Task 2's test.
- **Custom QEMU device `VMStateDescription` completeness**: Without `dc->vmsd` on `imx6.ipu`, `atmel.mxt`, and `rcd330-vip`, `savevm` either refuses to snapshot (`unmigratable`) or restores zeroed IPU `regs[]` (black screen!) and lost VIP state (`startup_synced = false`, `ign_state = 0`). Every custom device must register a valid `VMStateDescription` (and `imx6_ipu` must mark `have_prev_fb = false` in `post_load` to force an immediate full redraw); pinned in Task 3's test.
- **MTTCG / SMP safety of `imx6_ipu`, `rcd330_vip`, and `imx_gpt`**: Under `-smp 2`, two vCPUs can touch MMIO concurrently (though QEMU BQL serializes MMIO handlers, `imx6_ipu_gfx_update` and timer callbacks run under BQL while guest memory writes happen concurrently); verify `digprog` (`0x00610000` = i.MX6DL 2-core) and `a9mpcore` SCU CPU count (`smp_cpus = 2`) bring CPU1 online cleanly in Linux 3.0.35 without `imx_gpt` timer stalls; pinned in Task 4's test.
- **Snapshot staleness invalidation**: A cached VM snapshot taken from an older `zImage`, `rootfs.cpio.gz`, or `coding` JSON must never be silently restored after those inputs change; `hmi_qemu.py` must key/invalidate snapshot tags by `(zImage mtime/sha, rootfs sha, coding sha)`; pinned in Task 3's test.

---

### Task 1: Instant Concatenated Overlay Initramfs for `--hmi` / `--profile-env`

**Files:**
- Modify: `tools/build_rootfs.sh:155-198`
- Create: `tools/tests/test_emu_plan5_velocity.py`

**Interfaces:**
- Consumes: Cached stock `$CACHE_CPIO` (`rootfs.cpio.gz`) and `$STAGE_DIR` in `tools/build_rootfs.sh`.
- Produces:
  - Fast `build_variant_cpio()` writing a tiny gzipped overlay archive (`$overlay_cpio`) containing only replaced regular files (`./opt/hmi`, `./etc/profile`, `./opt/launch.sh`) and producing `$VARIANT_CPIO` via `cat "$CACHE_CPIO" "$overlay_cpio" > "$VARIANT_CPIO"` (with optional `--full-repack` fallback flag).

- [ ] **Step 1: Write failing test in `tools/tests/test_emu_plan5_velocity.py`**

```python
#!/usr/bin/env python3
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestEmuPlan5Velocity(unittest.TestCase):
    def test_build_rootfs_uses_concatenated_overlay_cpio(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        fn = build_sh.split("build_variant_cpio()")[1].split("\n}\n")[0]
        self.assertIn('cat "$CACHE_CPIO"', fn)
        self.assertNotIn('cd "$STAGE_DIR" && find .', fn)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan5_velocity.py -v`
Expected: FAIL (`build_variant_cpio` still re-scans `$STAGE_DIR`).

- [ ] **Step 3: Switch `build_variant_cpio()` in `tools/build_rootfs.sh` to concatenated gzipped overlay**

In `tools/build_rootfs.sh` (`build_variant_cpio()`):
```bash
    local overlay_gz="$overlay/overlay.cpio.gz"
    echo "Creating fast variant initramfs $VARIANT_CPIO (hmi: ${HMI_SRC:-stock}, profile env: ${#PROFILE_ENV[@]})..."
    (cd "$overlay" && find . -type f ! -name 'overlay.cpio.gz' -print0 | cpio -0 -o -H newc 2>/dev/null | gzip -1 > "$overlay_gz")
    rm -f "$VARIANT_CPIO"
    cat "$CACHE_CPIO" "$overlay_gz" > "$VARIANT_CPIO"
    rm -rf "$overlay"
```

- [ ] **Step 4: Run unit test and verify in a real QEMU boot (when `RCD330_QEMU_TESTS=1`)**

Run: `python3 tools/tests/test_emu_plan5_velocity.py -v`
And verify live overwrite in QEMU: `RCD330_QEMU_TESTS=1 python3 -m unittest tools/tests/test_hmi_boot.py -v`
Expected: PASS (variant staging completes in `< 0.5 s` and boots recreated `build/hmi/hmi`).

- [ ] **Step 5: Commit**

```bash
git add tools/build_rootfs.sh tools/tests/test_emu_plan5_velocity.py
git commit -m "tools/build_rootfs: build --hmi variant via concatenated overlay cpio.gz"
```

---

### Task 2: Non-Interactive UART2 (`ttymxc1`) Command Channel & In-Place `/opt/hmi` Hot-Reload

**Files:**
- Modify: `tools/arm_init/init.c:25-88`
- Modify: `tools/run_rcd330.sh:20-125,245-270`
- Modify: `tools/hmi_qemu.py:48-284`
- Modify: `tools/prebuilt/init` (via `tools/build_shims.sh`)
- Modify: `tools/tests/test_emu_plan5_velocity.py`

**Interfaces:**
- Consumes: `fsl-imx6` `s->soc.uart[1]` (`serial_hd(1)` → `/dev/ttymxc1` in guest Linux 3.0.35).
- Produces:
  - `--cmd-sock <unix-socket>` option in `tools/run_rcd330.sh` wiring `-serial stdio -serial unix:<sock>,server,nowait` (UART1 = console, UART2 = command channel).
  - Background command shell daemon in `tools/arm_init/init.c` on `/dev/ttymxc1` (`mknod /dev/ttymxc1 c 207 17`, running `/bin/sh` with `echo` disabled via `termios` so commands don't echo back).
  - `Rcd330Qemu.exec_guest(cmd, timeout=30.0) -> (rc, output)` in `tools/hmi_qemu.py` wrapping `cmd` with a unique sentinel `__RCD330_RC__:$?:<nonce>` to capture exact exit status + stdout/stderr, and `Rcd330Qemu.vmf_send(grp, sub, *bytes)` helper invoking `/opt/tools/vmf_send`.

- [ ] **Step 1: Add failing tests for `--cmd-sock`, `/dev/ttymxc1`, and `exec_guest` sentinel parser**

Append to `TestEmuPlan5Velocity` in `tools/tests/test_emu_plan5_velocity.py`:

```python
    def test_cmd_sock_uart2_wiring_and_exec_guest_helper(self):
        import subprocess
        import sys
        init_c = (ROOT / "tools/arm_init/init.c").read_text()
        self.assertIn("/dev/ttymxc1", init_c)
        self.assertIn("makedev(207, 17)", init_c)
        out = subprocess.check_output(
            [str(ROOT / "tools/run_rcd330.sh"), "--headless", "--dry-run", "--cmd-sock", "/tmp/cmd.sock"],
            text=True,
        )
        self.assertIn("unix:/tmp/cmd.sock,server,nowait", out)
        sys.path.insert(0, str(ROOT / "tools"))
        import hmi_qemu
        self.assertTrue(hasattr(hmi_qemu.Rcd330Qemu, "exec_guest"))
        self.assertTrue(hasattr(hmi_qemu.Rcd330Qemu, "vmf_send"))
        rc, body = hmi_qemu._parse_sentinel_output(
            "hello world\n__RCD330_RC__:0:abc123\n", "abc123"
        )
        self.assertEqual((rc, body), (0, "hello world\n"))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan5_velocity.py -v`
Expected: FAIL (`--cmd-sock` unknown and `exec_guest` missing).

- [ ] **Step 3: Implement `/dev/ttymxc1` command shell, `--cmd-sock`, and `exec_guest` / `vmf_send`**

1. In `tools/arm_init/init.c`:
   - Create `mknod("/dev/ttymxc1", S_IFCHR | 0666, makedev(207, 17));`.
   - Before spawning `/etc/rc.d/rcS`, fork a lightweight non-blocking background worker opening `/dev/ttymxc1` (`O_RDWR | O_NOCTTY`), clearing `ECHO` via `tcgetattr`/`tcsetattr` (`c_lflag &= ~(ECHO | ICANON)` or line mode with `~ECHO`), dup2'ing it to fds `0/1/2`, and exec'ing `/bin/sh` (with `PATH` and `LD_PRELOAD` inherited, respawning if exited).
   - Rebuild prebuilt init: `./tools/build_shims.sh --force --update-prebuilt`.
2. In `tools/run_rcd330.sh`:
   - Add `--cmd-sock <path>` / `--cmd-sock=<path>`.
   - Right after `-serial stdio`, emit `-serial "unix:${CMD_SOCK},server,nowait"` when `$CMD_SOCK` is set (otherwise `-serial null` so UART indices stay stable for future UART5/ALPS BT!).
3. In `tools/hmi_qemu.py`:
   - Pass `--cmd-sock <tmpdir>/cmd.sock` automatically in `Rcd330Qemu.start()`.
   - Add `_parse_sentinel_output(raw, nonce)` and `exec_guest(self, cmd, timeout=30.0)`: connects to `self.cmd_path`, sends `f"({cmd}) 2>&1; printf '\\n__RCD330_RC__:%d:{nonce}\\n' $?\n"`, reads until `__RCD330_RC__:<rc>:<nonce>\n`, returns `(rc, output)`.
   - Add `vmf_send(self, group, sub, *payload_bytes)` calling `self.exec_guest(...)` with `/opt/tools/vmf_send` (checking for executable presence and raising a clear error on firmware dumps like RCD 780 that omit `/opt/tools/vmf_send`).

- [ ] **Step 4: Run tests**

Run: `python3 tools/tests/test_emu_plan5_velocity.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_init/init.c tools/prebuilt/init tools/run_rcd330.sh tools/hmi_qemu.py tools/tests/test_emu_plan5_velocity.py
git commit -m "tools: add UART2 (/dev/ttymxc1) non-interactive guest exec channel and vmf_send helper"
```

---

### Task 3: QEMU VMState Support (`savevm` / `loadvm`) for Sub-2-Second Boot Restore

**Files:**
- Modify: `qemu/hw/display/imx6_ipu.c:50-66,320-358`
- Modify: `qemu/hw/input/atmel_mxt.c:40-56,350-375`
- Modify: `qemu/hw/misc/rcd330_vip.c:44-146,1605-1638`
- Modify: `qemu/hw/arm/rcd330.c:130-195,255-280`
- Modify: `tools/run_rcd330.sh:20-125,245-270`
- Modify: `tools/hmi_qemu.py:48-284`
- Modify: `tools/tests/test_emu_plan5_velocity.py`

**Interfaces:**
- Consumes: QEMU `migration/vmstate.h` (`VMStateDescription`, `VMSTATE_*`, `dc->vmsd`).
- Produces:
  - `VMStateDescription` (`vmstate_imx6_ipu`, `vmstate_atmel_mxt`, `vmstate_rcd330_vip`, `vmstate_adv7182`) registered on each custom device's `DeviceClass::vmsd` (with `imx6_ipu_post_load` setting `s->have_prev_fb = false` so the display refreshes immediately on `loadvm`, and `rcd330_vip_post_load` preserving `pss_warm_ram`, `coding`, `ign_state`, `startup_synced`, and timers).
  - `--snapshot-img <qcow2>` flag in `tools/run_rcd330.sh` attaching a tiny 16 MB qcow2 block device (`-drive if=none,id=vmstate,file=<qcow2>,format=qcow2`) so QEMU `savevm <tag>` / `loadvm <tag>` works cleanly with `-initrd`.
  - `Rcd330Qemu.save_snapshot(tag)` and `Rcd330Qemu.load_snapshot(tag)` via the QEMU monitor socket, plus `snapshot_cache_key(zimage, rootfs, coding_file)` invalidation helper.

- [ ] **Step 1: Add failing tests for `VMStateDescription` registration and snapshot helpers**

Append to `TestEmuPlan5Velocity` in `tools/tests/test_emu_plan5_velocity.py`:

```python
    def test_custom_qemu_devices_define_vmstate_and_snapshot_cli(self):
        for rel, sym in (
            ("qemu/hw/display/imx6_ipu.c", "vmstate_imx6_ipu"),
            ("qemu/hw/input/atmel_mxt.c", "vmstate_atmel_mxt"),
            ("qemu/hw/misc/rcd330_vip.c", "vmstate_rcd330_vip"),
            ("qemu/hw/arm/rcd330.c", "vmstate_adv7182"),
        ):
            src = (ROOT / rel).read_text()
            self.assertIn(sym, src, f"{sym} missing in {rel}")
            self.assertIn("dc->vmsd", src, f"dc->vmsd missing in {rel}")
        out = subprocess.check_output(
            [str(ROOT / "tools/run_rcd330.sh"), "--headless", "--dry-run", "--snapshot-img", "/tmp/snap.qcow2"],
            text=True,
        )
        self.assertIn("id=vmstate,file=/tmp/snap.qcow2,format=qcow2", out)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan5_velocity.py -v`
Expected: FAIL (`vmstate_*` and `--snapshot-img` missing).

- [ ] **Step 3: Add `VMStateDescription` to all 4 custom devices and snapshot helpers to `run_rcd330.sh` / `hmi_qemu.py`**

1. In `qemu/hw/display/imx6_ipu.c`:
   - Include `"migration/vmstate.h"`.
   - Define `imx6_ipu_post_load`: sets `s->have_prev_fb = false; s->prev_bpp = 0; imx6_ipu_update_irq(s); return 0;`.
   - Define `vmstate_imx6_ipu` saving `VMSTATE_VARRAY_UINT32_ALLOC` or `VMSTATE_VBUFFER_UINT32(regs, IMX6IPUState, 1, NULL, IMX6_IPU_SIZE)` (saving the `4 MB` `regs` array) and `VMSTATE_TIMER_PTR(vsync_timer, IMX6IPUState)`, attach `dc->vmsd = &vmstate_imx6_ipu;`.
2. In `qemu/hw/input/atmel_mxt.c`:
   - Include `"migration/vmstate.h"`.
   - Define `vmstate_atmel_mxt` saving `VMSTATE_I2C_SLAVE(parent_obj, AtmelMxtState)`, `mem`, `reg_addr`, `addr_bytes`, `cur_x`, `cur_y`, `touching`, `prev_touching`, `dirty`; attach `dc->vmsd = &vmstate_atmel_mxt;`.
3. In `qemu/hw/misc/rcd330_vip.c`:
   - Include `"migration/vmstate.h"`.
   - Define `vmstate_rcd330_vip` saving `VMSTATE_SSI_PERIPHERAL(parent_obj, Rcd330VipState)`, timers (`srq_timer`, `knob_timer`, `pwr_hold_timer`), `startup_synced`, power/clamp/telemetry fields, EEPROM fields (`platform`, `brand`, `vehicle`, `can_speed`, `part_num`, `hw_ver`, `serial_num`, `coding`, `coding_len`, `speakers`, `eng_mode`, `ops_sensors`, `pss_warm_ram`), tuner state, and `phase`/`hdr_idx`/`data_idx`. Attach `dc->vmsd = &vmstate_rcd330_vip;`.
4. In `qemu/hw/arm/rcd330.c`:
   - Include `"migration/vmstate.h"`.
   - Add `vmstate_adv7182` saving `VMSTATE_I2C_SLAVE(parent_obj, Adv7182StubState)`, `regs`, `reg_ptr`, `have_reg`; attach `dc->vmsd = &vmstate_adv7182;`.
5. In `tools/run_rcd330.sh`:
   - Add `--snapshot-img <qcow2>` (auto-creating a 16 MB empty qcow2 via `qemu/build/qemu-img create -f qcow2 "$SNAP_IMG" 16M` when missing and `DRY_RUN == 0`) and pass `-drive "if=none,id=vmstate,file=$SNAP_IMG,format=qcow2"`.
6. In `tools/hmi_qemu.py`:
   - Add `monitor_cmd(cmd)` helper, `save_snapshot(tag)` (`savevm <tag>`), `load_snapshot(tag)` (`loadvm <tag>`), and `snapshot_cache_key(paths...)` computing a short SHA-256 fingerprint across `zImage`, `rootfs`, and `coding_file`.

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan5_velocity.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/display/imx6_ipu.c hw/input/atmel_mxt.c hw/misc/rcd330_vip.c hw/arm/rcd330.c
git -C qemu commit -m "hw/rcd330: add VMStateDescription to custom devices for savevm/loadvm snapshots"
git add tools/run_rcd330.sh tools/hmi_qemu.py tools/tests/test_emu_plan5_velocity.py
git commit -m "tools: add --snapshot-img and Rcd330Qemu save_snapshot/load_snapshot support"
```

---

### Task 4: Optional Dual-Core i.MX6DL SMP Bring-Up (`--smp 2`)

**Files:**
- Modify: `qemu/hw/arm/rcd330.c:215-240,371-383`
- Modify: `tools/run_rcd330.sh:15-130,235-245`
- Modify: `tools/tests/test_emu_plan5_velocity.py`

**Interfaces:**
- Consumes: Existing `TYPE_IMX6_SRC` (`s->soc.src`) and `TYPE_A9MPCORE_PRIV` (`s->soc.a9mpcore`) in `FslIMX6State` (`qemu/hw/arm/fsl-imx6.c`), plus `SMP PREEMPT` in stock `5624`/`5606` Linux 3.0.35 kernels.
- Produces:
  - Verified i.MX6DL dual-core cap (`mc->max_cpus = 2; mc->default_cpus = 1;` matching `digprog = 0x00610000` DualLite — previously `FSL_IMX6_NUM_CPUS = 4` allowed invalid 4-core configs on a DL `digprog`!) in `qemu/hw/arm/rcd330.c`.
  - `--smp <1|2>` flag (and `QEMU_SMP` env override, default `1`) in `tools/run_rcd330.sh`.

- [ ] **Step 1: Add failing tests for `--smp` flag and DualLite `max_cpus = 2` cap**

Append to `TestEmuPlan5Velocity` in `tools/tests/test_emu_plan5_velocity.py`:

```python
    def test_smp_flag_and_imx6dl_max_cpus(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        self.assertIn("mc->max_cpus = 2;", rcd330_c)
        out = subprocess.check_output(
            [str(ROOT / "tools/run_rcd330.sh"), "--headless", "--dry-run", "--smp", "2"],
            text=True,
        )
        self.assertIn("-smp 2", out)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan5_velocity.py -v`
Expected: FAIL (`mc->max_cpus = 2;` and `--smp` not present).

- [ ] **Step 3: Cap `rcd330` at 2 cores (i.MX6DL) and expose `--smp <1|2>` in `tools/run_rcd330.sh`**

1. In `qemu/hw/arm/rcd330.c` (`rcd330_machine_class_init`):
```c
    /* i.MX6DL (digprog 0x00610000) is dual-core Cortex-A9 */
    mc->max_cpus = 2;
    mc->default_cpus = 1;
```
2. In `tools/run_rcd330.sh`:
   - Add `SMP_CPUS="${QEMU_SMP:-1}"` and `--smp <1|2>` / `--smp=<1|2>` flag validation (`[[ "$SMP_CPUS" == "1" || "$SMP_CPUS" == "2" ]]`).
   - Pass `-smp "$SMP_CPUS"` in `QEMU_CMD`.
3. Rebuild QEMU and validate dual-core Linux boot (`CPU1: Booted secondary processor` + `test_hmi_boot.py` under `QEMU_SMP=2 RCD330_QEMU_TESTS=1`).

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan5_velocity.py -v`
And slow verification: `QEMU_SMP=2 RCD330_QEMU_TESTS=1 python3 -m unittest tools/tests/test_hmi_boot.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/arm/rcd330.c
git -C qemu commit -m "hw/arm/rcd330: set i.MX6DL DualLite max_cpus=2 and default_cpus=1"
git add tools/run_rcd330.sh tools/tests/test_emu_plan5_velocity.py
git commit -m "tools/run_rcd330: add --smp <1|2> option for dual-core i.MX6DL emulation"
```
