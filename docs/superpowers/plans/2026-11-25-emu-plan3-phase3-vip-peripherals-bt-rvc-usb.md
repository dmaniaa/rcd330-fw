# Emulator Plan 3: Phase 3 Peripherals (Virtual ALPS BT UART, Expanded VIP, RVC Overlay & USB Host)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the emulated peripherals needed to test HMI Phase 3 screens (Phone/BT, Tuner, Sound, Car/MFA/Climate, Popups, Projection, Camera, USB Media) and reproduce the Android Auto Bluetooth ECNR echo bug (`docs/bugs/android-auto-echo-ecnr.md`): a virtual ALPS C210 Bluetooth UART backend on UART5 (`/dev/ttymxc4`), expanded `rcd330-vip` Phase 3 IPCL handlers, real `gpio44` + IPUv3 foreground overlay (`/dev/fb1`) RVC compositing, USB host storage (`--usb`) via `fsl-imx6`'s `TYPE_CHIPIDEA`, and the full `rcd330-vip` AM/FM tuner peer (`IPCL 0x05:0x01`, `0x3f` preset block fix, `HOSTCMD`/`TUNER_TRACE`, and `tools/hmi_qemu.py` helpers required by HMI Phase 3a).

**Architecture:** Today `/dev/ttymxc4` is stubbed with a silent blocking pipe in `hal_gpio_shim.c` (so `opt/bt` hangs at step 1 of `BtConfigInitSM`), `rcd330-vip` handles only a minimal subset of Tuner/DSP/BAP/Diag commands (and mis-sends RDS radiotext on `0x3f` instead of the 6×12-byte preset block), `gpio44` is hardcoded to `"1\n"` while `imx6_ipu.c` only scans out Channel 23 (`/dev/fb0`), and USB mass storage is unwired even though `fsl-imx6.c` already instantiates `TYPE_CHIPIDEA` and both stock kernels ship `fsl-ehci` + `usb-storage`. We attach a host virtual ALPS C210 UART emulator (`tools/rcd330_alps_bt.py`) directly to QEMU's real i.MX6 UART5 (`serial_hd(4)` → `/dev/ttymxc4`), expand `rcd330_vip.c` with verified Phase 3 DSP/BAP/Diag/AUX/eCall/SafeCode/Backlight/Sysupdate handlers, connect `REV` to `GPIO2_12` (`gpio44`) and add IPUv3 foreground overlay (`MEM_FG_SYNC` ch 27, global/local alpha, backlight dimming) in `imx6_ipu.c`, expose `--usb <disk.img>` plus matching Qt6 GUI controls, and implement the full VIP AM/FM tuner peer (`05:01` command set, timer-driven seek/scan/auto-store, `HOSTCMD`, `TUNER_TRACE`) plus `Rcd330Qemu` helpers (`hostcmd`, `tuner_trace`, `tuner_events`, `drag`) verified by `tools/tests/test_vip_tuner.py`.

**Tech Stack:** Python 3 (`tools/rcd330_alps_bt.py`, `tools/hmi_qemu.py`, `tools/tests/test_emu_plan3_peripherals.py`, `tools/tests/test_vip_tuner.py`), C (`qemu/hw/misc/rcd330_vip.c`, `qemu/hw/display/imx6_ipu.c`, `qemu/hw/arm/rcd330.c`, `tools/arm_hal_shim/hal_gpio_shim.c`), C++20/Qt6 (`tools/rcd330_gui/`), Bash (`tools/run_rcd330.sh`).

**Spec:** `docs/architecture.md`, `docs/ipc-and-vgw.md`, `docs/coding.md`, `docs/bugs/android-auto-echo-ecnr.md`, `docs/decompiled/5624/services/bt/functions/0x28d80.c`, `docs/decompiled/5624/services/bt/functions/0x5895c.c`, `docs/decompiled/5624/vip/functions/0x366b0.c`, `docs/decompiled/5624/services/camera/functions/0x8adc.c`, `docs/decompiled/5624/hmi/functions/0xae28c.c`, `docs/decompiled/5624/hmi/functions/0x1232e4.c`, `docs/decompiled/5624/hmi/functions/0x12226c.c`, `docs/decompiled/5624/hmi/functions/0x121ad8.c`.

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- All QEMU device edits live in the `qemu/` submodule (`rcd330` branch) and are committed there separately.
- Keep `tools/prebuilt/libhal_gpio_shim.so` updated via `./tools/build_shims.sh --force --update-prebuilt` after removing the `/dev/ttymxc4` and `gpio44` shims from `hal_gpio_shim.c`.
- `serial_hd(0..4)` ordering in `tools/run_rcd330.sh` must remain deterministic: `0` = `ttymxc0` (`stdio` console), `1` = `ttymxc1` (command channel or `null`), `2` = `ttymxc2` (`null`), `3` = `ttymxc3` (`null`), `4` = `ttymxc4` (ALPS C210 BT UART socket or `null`).
- Maintain backwards compatibility with existing `rcd330_vip` commands and `REV 1` behavior (defaulting to working RVC even when no external V4L2 camera stream is active).

## Review Focus

- **ALPS UART5 transport framing & ACK sequencing (`opt/bt` `0x5895c`, `0x57dd4`, `0x57ce4`)**: Every packet from `opt/bt` carries a 4-byte header (`[flags, len_lo, (seq<<4)|(crc_en<<3)|len_hi, sum(hdr[0..2])&0xff]`), LE16 raw len, channel byte, `[LE16 opcode, LE16 arg_len, args...]`, and optional reflected CRC-16 (`0x57ce4`, table at `opt/bt VA 0x9f2e8`). If the virtual ALPS backend fails to ACK the 3-bit `tx_seq` (`0..7`) or returns a mismatched CRC, `opt/bt` stalls after 1 packet; pinned in Task 1's unit tests.
- **ECNR state-machine auditability (`0x0331`)**: The virtual ALPS device must record every `0x0331 <00|01>`, `0x0343 <00|01>`, and `0x0388 <src>` command received and expose current `ecnr_enabled`, `audio_path`, and `codec_src` via its control socket (`BT STATUS`) so automated tests can deterministically catch the Android Auto echo bug (`ecnr_enabled == 0` during an active HFP call); pinned in Task 1's test.
- **PQ `0x1f48a` coding rejection in `rcd330-vip`**: Live `CODING <hex>` writes for PQ (`platform == 1`, 7 bytes) must validate against the exact `vipImage.bin 0x1f48a` rules (all-zero rejected, `b0 & 0xE0 == 0`, `b1 in {0,2}` coupled with `b2`/`b4`, `b3 & 0x9D == 0`, `b5 == 0`, `b6 < 0x40`) and return `ERR CODING_INVALID\n` (matching VIP NRC `0x31`) on invalid bytes rather than forwarding illegal coding to `opt/diag`; pinned in Task 2's test.
- **`gpio44` active-low polarity for `opt/camera` (`0x8adc` / `0x9bdc`)**: `opt/camera` spins in `while (read_gpio44() != 0) usleep(50000);` — i.e. `gpio44 == 1` means forward/idle and `gpio44 == 0` means reverse engaged! Driving `GPIO2_12` (`gpio44`) HIGH on reset and LOW when `REV 1` is active ensures `opt/camera` never fires spuriously at cold boot; pinned in Task 3's test.
- **IPUv3 `/dev/fb1` foreground plane disabled by default**: When Channel 27 (`MEM_FG_SYNC`) is not enabled in `IPU_IDMAC_CH_EN_1` (`bit 27 == 0`), `imx6_ipu_gfx_update` must incur zero extra DMA reads and render Channel 23 (`/dev/fb0`) identically to today; pinned in Task 3's test.
- **VIP tuner `0x3f` preset block & `TUNER_TRACE` reply sync**: `rcd330_vip.c` currently sends RDS radiotext on `0x3f`, whereas `VAMFMProxy::RecieveVmfMsg` (`0x000ae28c`) and `VTunerMainPageImpl_::UpdatePresetsInfo` (`0x001232e4`) read `0x3f` (`0x48` bytes) as 6 × 12-byte presets; and `TUNER_TRACE 1` async `EVT TUNERCMD`/`EVT TUNER` lines must never desync synchronous `vip()` replies (`_drain_vip` separates `EVT ` lines); pinned in Task 5's tests (`test_vip_tuner.py`).

---

### Task 1: Virtual ALPS C210 Bluetooth Module on UART5 (`tools/rcd330_alps_bt.py`) & Remove `/dev/ttymxc4` Shim

**Files:**
- Create: `tools/rcd330_alps_bt.py`
- Modify: `tools/arm_hal_shim/hal_gpio_shim.c:214-296,500-545`
- Modify: `tools/run_rcd330.sh:15-130,245-309`
- Modify: `tools/prebuilt/libhal_gpio_shim.so` (via `tools/build_shims.sh`)
- Create: `tools/tests/test_emu_plan3_peripherals.py`

**Interfaces:**
- Consumes: QEMU `-serial unix:<bt_uart.sock>,server,nowait` mapped to `serial_hd(4)` (`s->soc.uart[4]` → `/dev/ttymxc4`, opened by `opt/bt` `0x59388` at 921600 8N1).
- Produces:
  - `tools/rcd330_alps_bt.py`: standalone async/select host daemon implementing:
    1. Exact ALPS UART transport framing from `opt/bt` (`0x5895c`, `0x57dd4` 8-bit header sum, `0x57ce4` reflected CRC-16, 3-bit `tx_seq` ACK).
    2. Complete `BtConfigInitSM` (`0x28d80`) command/confirm handshake (`0x0200` config params `0x0306`/`0x0308`/`0x5007`/`0x0352`, local name `0x107`, support service `0x10b`, voice/ECNR `0x0331` → CFM `0x138`, audio path `0x0343` → CFM `0x13c`, codec src `0x0388` → CFM `0x13d`, ECNR params `0x0332`, telephony dataset `0x0345`, EQ `0x0351`, AVRCP browsing `0x2012`, version report `C210`).
    3. Line-based control socket (`--ctl-port` / `--ctl-sock`): `BT STATUS` (returns `ECNR=<0|1> PATH=<0|1> CODEC=<n> CONNECTED=<0|1> CALL=<state>`), `BT CONNECT <name> <mac>`, `BT DISCONNECT`, `BT CALL INCOMING <number> <name>`, `BT CALL ANSWER`, `BT CALL HANGUP`, `BT A2DP <title>|<artist>|<album>`.
  - Removal of `/dev/ttymxc4` pipe stub + `bt_version_announce_thread` from `tools/arm_hal_shim/hal_gpio_shim.c` (gated on env `QEMU_ALPS_UART=1` during transition, default enabled when `run_rcd330.sh` starts `rcd330_alps_bt.py`).

- [ ] **Step 1: Write failing unit tests for ALPS framing, `BtConfigInitSM`, and ECNR tracking in `tools/tests/test_emu_plan3_peripherals.py`**

```python
#!/usr/bin/env python3
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


class TestEmuPlan3Peripherals(unittest.TestCase):
    def test_alps_bt_transport_framing_and_ecnr_state_machine(self):
        import rcd330_alps_bt as alps

        # Build a valid 0x5895c-framed 0x0331 00 (ECNR OFF) command and decode it
        pkt = alps.encode_transport_frame(opcode=0x0331, payload=b"\x00", seq=1, crc_en=True)
        decoded = alps.decode_transport_frame(pkt)
        self.assertEqual(decoded.opcode, 0x0331)
        self.assertEqual(decoded.payload, b"\x00")
        self.assertEqual(decoded.seq, 1)

        emu = alps.AlpsC210Emulator()
        # Boot init enables ECNR (0x0331 01)
        replies = emu.handle_command(0x0331, b"\x01")
        self.assertTrue(any(r.event_id == 0x138 for r in replies))
        self.assertEqual(emu.ecnr_enabled, 1)

        # AA VR mode=5 sequence: 0x0388 02 -> 0x0343 00 -> 0x0331 00 -> 0x0343 01
        r1 = emu.handle_command(0x0388, b"\x02")
        self.assertTrue(any(r.event_id == 0x13D for r in r1))
        r2 = emu.handle_command(0x0343, b"\x00")
        self.assertTrue(any(r.event_id == 0x13C for r in r2))
        emu.handle_command(0x0331, b"\x00")
        emu.handle_command(0x0343, b"\x01")
        self.assertEqual(emu.ecnr_enabled, 0)
        self.assertEqual(emu.audio_path, 1)
        self.assertEqual(emu.codec_src, 2)
        self.assertIn("ECNR=0", emu.process_ctl("BT STATUS"))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan3_peripherals.py -v`
Expected: FAIL (`ModuleNotFoundError: No module named 'rcd330_alps_bt'`).

- [ ] **Step 3: Implement `tools/rcd330_alps_bt.py`, wire UART5 in `tools/run_rcd330.sh`, and gate `/dev/ttymxc4` shim**

1. Create `tools/rcd330_alps_bt.py`:
   - Implement `alps_hdr_checksum(hdr3)` (`sum(hdr3) & 0xff`, matching `0x57dd4`), `alps_crc16(data)` (reflected CRC-16 matching `opt/bt 0x57ce4` / table `0x9f2e8`), `encode_transport_frame(...)`, and `decode_transport_frame(...)`.
   - Implement `AlpsC210Emulator`:
     - Tracks `ecnr_enabled` (default `1`), `audio_path` (`0`), `codec_src` (`0`), `local_name`, `support_service`, `connected_dev`, `call_state`, `a2dp_meta`.
     - Handles every `BtConfigInitSM` opcode (`0x0200` sub-configs → event `0x12c` / `300`, local name → `0x107`, support service → `0x10b`, `0x0331` → `0x138`, `0x0343` → `0x13c`, `0x0388` → `0x13d`, `0x0332`/`0x0345`/`0x0351`/`0x2012` → OK confirms, and firmware version query returning `C210`).
     - Handles control socket commands `BT STATUS`, `BT CONNECT`, `BT DISCONNECT`, `BT CALL INCOMING/ANSWER/HANGUP`, `BT A2DP`, emitting the corresponding ALPS unsolicited indications over UART5.
   - Add CLI entrypoint `python3 tools/rcd330_alps_bt.py --uart-sock <unix_sock> --ctl-port <port>` using `selectors.DefaultSelector`.
2. In `tools/arm_hal_shim/hal_gpio_shim.c`:
   - In `open()` / `open64()` for `/dev/ttymxc4`: if `getenv("QEMU_ALPS_UART")` is `"1"`, pass through to `real_open("/dev/ttymxc4", ...)` so `opt/bt` talks directly to QEMU's `s->soc.uart[4]`. Otherwise keep the legacy pipe fallback.
   - Rebuild prebuilt shim: `./tools/build_shims.sh --force --update-prebuilt`.
3. In `tools/run_rcd330.sh`:
   - Add `--bt-emu` (and `QEMU_BT_PORT`) flag: launches `tools/rcd330_alps_bt.py` attached to UART5 (`-serial unix:<tmpdir>/alps_bt.sock,server,nowait` at slot 4, padding slots 1..3 with `cmd.sock`/`null`) and sets `QEMU_ALPS_UART=1` via `--profile-env`.

- [ ] **Step 4: Run tests**

Run: `python3 tools/tests/test_emu_plan3_peripherals.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/rcd330_alps_bt.py tools/arm_hal_shim/hal_gpio_shim.c tools/prebuilt/libhal_gpio_shim.so tools/run_rcd330.sh tools/tests/test_emu_plan3_peripherals.py
git commit -m "tools: add virtual ALPS C210 BT UART5 emulator with ECNR state tracking"
```

---

### Task 2: Expand `rcd330-vip` Phase 3 Protocols (DSP, AUX/eCall/SafeCode, Diag/Adapt, Backlight/Sysupdate)

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c:95-145,305-445,618-843,1061-1381,1538-1595`
- Modify: `tools/tests/test_emu_plan3_peripherals.py`

**Interfaces:**
- Consumes: Verified opcodes from `vipImage.bin` (`0x1f48a` PQ coding validator, `0x1d64a` diag), `opt/audio_proc` (`DAT_00038528` DSP commands `0x00..0x18`), `docs/ipc-and-vgw.md`, and `docs/coding.md`. *(Full AM/FM tuner `IPCL 0x05:0x01` peer is implemented separately in Task 5.)*
- Produces:
  - **Audio DSP (`IPCL 0x03:0x00` → `0x03:0x02`)**: Generic ACK (`pl[0] | 0x40`) for `0x03` (`phone_volume`), `0x04..0x06` (`aux/bta/ipod_lvl`), `0x07` (`bal`), `0x08` (`fad`), `0x09..0x0c` (`geq/bass/mid/treb`), `0x0d..0x13` (`mic_mute/db/test/loudness/sys_tone`), storing bass/mid/treble/bal/fad and notifying host (`AUDIO VOL=... MUTE=... BAL=... FAD=...`).
  - **AUX / eCall / SafeCode / Delivery / Odometer / MFA / Backlight / Sysupdate**:
    - Control commands: `AUX <0|1>` (`IPCL 0x08:0x02 [0x82, aux]`), `ECALL <0|1>` (`IPCL 0x08:0x02 [0x90, ecall]`), `SAFECODE <0|1>` (`IPCL 0x06:0x0C [0x01, sc]`), `DELIVERY <0|1>` (`IPCL 0x06:0x0C [0x02, del]`), `ODO <km>` (`IPCL 0x06:0x0C [0x03, b2, b1, b0]`), `MFA <avg_spd> <avg_cons> <range_km> <trip_km>`.
    - Host-TX handling: `IPCL 0x08:0x03 [0x04/0x05]` emits `BACKLIGHT <1|0>\n` to host; `IPCL 0x0B:0x02 [0x01]` triggers `qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)` (`REBOOT SYSUPDATE\n`); `IPCL 0x01:0x0C` logs `USB_PWR` / `DISP_PWR` cleanly; `IPCL 0x07:0x00` handles `actionId=9` (Ch 36 Threshold_High) and `actionId=10` (Ch 47 MirrorLink country code); `CODING <hex>` enforces exact `vipImage.bin 0x1f48a` PQ validation (`ERR CODING_INVALID\n`).

- [ ] **Step 1: Add failing tests for expanded VIP commands and `0x1f48a` validation in `tools/tests/test_emu_plan3_peripherals.py`**

Append to `TestEmuPlan3Peripherals`:
```python
    def test_vip_implements_phase3_dsp_aux_ecall_safecode_diag_and_pq_validation(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        for tok in (
            "vip_validate_pq_coding",
            "ERR CODING_INVALID\\n",
            'sscanf(line, "AUX %d"',
            'sscanf(line, "ECALL %d"',
            'sscanf(line, "SAFECODE %d"',
            'sscanf(line, "ODO %d"',
            "BACKLIGHT ",
            "REBOOT SYSUPDATE\\n",
            "grp == 0x0b && sub == 0x02",
            "case 0x07:",  # DSP bal
            "case 0x08:",  # DSP fad
            "case 0x0a:",  # DSP bass
            "pl[3] == 0x09",  # Diag Ch 36
            "pl[3] == 0x0a",  # Diag Ch 47
        ):
            self.assertIn(tok, vip_c, f"Missing {tok} in rcd330_vip.c")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan3_peripherals.py -v`
Expected: FAIL (`vip_validate_pq_coding` and Phase 3 handlers missing).

- [ ] **Step 3: Implement Phase 3 handlers in `qemu/hw/misc/rcd330_vip.c`**

1. Add `aux_connected`, `ecall_mute_pin`, `safe_code_locked`, `delivery_mode`, `odometer_km`, `backlight_on`, `threshold_high` (default `5`), `ml_country_code` (default `0`), and DSP state (`dsp_bal`, `dsp_fad`, `dsp_bass`, `dsp_mid`, `dsp_treb`) to `Rcd330VipState`.
2. Add `static bool vip_validate_pq_coding(const uint8_t b[7])` implementing the exact rules from `vipImage.bin 0x1f48a` (`docs/coding.md` §3.1) and call it on `CODING <hex>` when `s->platform == 1 && n == 7` (returning `ERR CODING_INVALID\n` on failure).
3. Extend `grp == 0x03 && sub == 0x00` DSP `switch (pl[0])` with cases `0x03..0x13` acknowledging `[pl[0] | 0x40, pl[1], pl[2], 0x00]` and emitting `AUDIO ...` host notifications on volume/mute/bal/fad/EQ updates.
4. Extend `grp == 0x07 && sub == 0x00` for `pl[3] == 0x09` (Ch 36 `threshold_high`) and `pl[3] == 0x0a` (Ch 47 `ml_country_code`).
5. Update `grp == 0x08 && sub == 0x03`:
   - `pl[0] == 0x03` returns `{ 0x82, s->aux_connected ? 1 : 0 }`.
   - `pl[0] == 0x04 || pl[0] == 0x05` sets `s->backlight_on = (pl[0] == 0x04)` and calls `vip_notify_host(s, s->backlight_on ? "BACKLIGHT 1\n" : "BACKLIGHT 0\n")`.
   - `pl[0] == 0x19` returns `{ 0x90, s->ecall_mute_pin ? 1 : 0 }`.
6. Add `grp == 0x0b && sub == 0x02 && plen >= 1 && pl[0] == 0x01` handler issuing `vip_notify_host(s, "REBOOT SYSUPDATE\n"); qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);`.
7. Add `AUX`, `ECALL`, `SAFECODE`, `DELIVERY`, `ODO`, and `MFA` commands to `vip_process_cmd`.

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan3_peripherals.py -v && python3 tools/tests/test_bugfixes_and_coding.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: add Phase 3 DSP, AUX, eCall, SafeCode, Diag, backlight, and 0x1f48a validation"
git add tools/tests/test_emu_plan3_peripherals.py
git commit -m "tests: add Plan 3 expanded VIP protocol coverage checks"
```

---

### Task 3: Reverse Camera Hardware Path (`gpio44` Wiring + IPUv3 `/dev/fb1` Foreground Overlay & Backlight Dimming)

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c:44-60,1134-1149,1505-1515,1596-1606`
- Modify: `qemu/hw/arm/rcd330.c:315-330`
- Modify: `qemu/hw/display/imx6_ipu.c:27-65,180-290`
- Modify: `tools/arm_hal_shim/hal_gpio_shim.c:475-490`
- Modify: `tools/prebuilt/libhal_gpio_shim.so` (via `tools/build_shims.sh`)
- Modify: `tools/tests/test_emu_plan3_peripherals.py`

**Interfaces:**
- Consumes: `opt/camera` (`0x8adc` / `0x9bdc` reading `/sys/class/gpio/gpio44/value`, active-low: `0` = reverse engaged, `1` = idle); `opt/hmi` (`0x5f4b0` `MXCFB_SET_LOC_ALPHA`, `0x5f554` `MXCFB_SET_GBL_ALPHA`, `0x5f934` `MXCFB_SET_CLR_KEY`) writing IPUv3 DP registers (`IPU_DP_BASE = 0x00218000`: `DP_COM_CONF_SYNC +0x00`, `DP_GRAPH_WIND_CTRL_SYNC +0x04`, `DP_FG_POS_SYNC +0x08`) and CPMEM Channel 27 (`MEM_FG_SYNC`).
- Produces:
  - Active-low `"rev-gpio"` output on `rcd330-vip` connected to `s->soc.gpio[1]` pin `12` (`GPIO2_12` = Linux `gpio44`), idling HIGH (`1`) on reset and driving LOW (`0`) when `REV 1` is active (plus `mmio[0x18 / 4] & 4u` bit for `hal_gpio_shim.c`'s `/tmp/gpio_emu/gpio44_value` sync when sysfs GPIO is shimmed).
  - Foreground overlay compositing in `qemu/hw/display/imx6_ipu.c`: when `IPU_IDMAC_CH_EN_1 & (1u << 27)` (`MEM_FG_SYNC`) is enabled, reads Channel 27 framebuffer (`IPU_CPMEM_BASE + 27 * 0x40 + 0x20`) and composites it with Channel 23 (`MEM_BG_SYNC`) using `DP_COM_CONF_SYNC` (`GWAM` global vs local alpha, `GWCKE` color key) and `DP_GRAPH_WIND_CTRL_SYNC` (`GWAV` alpha, `GWCKR/G/B` key).

- [ ] **Step 1: Add failing tests for `gpio44` wiring and IPUv3 Channel 27 overlay compositing**

Append to `TestEmuPlan3Peripherals` in `tools/tests/test_emu_plan3_peripherals.py`:
```python
    def test_rvc_gpio44_wiring_and_ipu_fg_overlay_channel27(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        ipu_c = (ROOT / "qemu/hw/display/imx6_ipu.c").read_text()
        self.assertIn('"rev-gpio"', rcd330_c)
        self.assertIn("s->soc.gpio[1]), 12", rcd330_c)
        self.assertIn("rev_gpio_irq", vip_c)
        self.assertIn("IPU_DP_BASE", ipu_c)
        self.assertIn("27 * 0x40", ipu_c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan3_peripherals.py -v`
Expected: FAIL (`rev-gpio` and `IPU_DP_BASE` missing).

- [ ] **Step 3: Wire `rev-gpio` (`GPIO2_12` = `gpio44`) and add IPUv3 Channel 27 overlay compositing**

1. In `qemu/hw/misc/rcd330_vip.c`:
   - Add `qemu_irq rev_gpio_irq;` initialized as `"rev-gpio"` in `vip_realize`, raised HIGH (`1`, idle/forward) in `vip_reset_exit`, and set to `s->rev_gear ? 0 : 1` whenever `REV` changes.
   - Expose `(s->rev_gear ? 0u : 4u)` as bit 2 of MMIO `0x18` (`gpio44` live state) so `hal_gpio_shim.c` returns live `"0\n"`/`"1\n"` whenever `/sys/class/gpio/gpio44/value` is opened!
2. In `qemu/hw/arm/rcd330.c`:
   - Connect `vip` `"rev-gpio"` to `qdev_get_gpio_in(DEVICE(&s->soc.gpio[1]), 12)` (`GPIO2_12` = Linux `gpio44`).
3. In `tools/arm_hal_shim/hal_gpio_shim.c`:
   - In `open_gpio_emu`, when `strstr(mapped_path, "gpio44_value")`, refresh the file content from `mmio[0x18 / 4] & 4u` (`"1\n"` when idle, `"0\n"` when reverse engaged) before returning the fd.
   - Rebuild prebuilt shim: `./tools/build_shims.sh --force --update-prebuilt`.
4. In `qemu/hw/display/imx6_ipu.c`:
   - Define `IPU_DP_BASE 0x00218000`, `DP_COM_CONF_SYNC (IPU_DP_BASE + 0x00)`, `DP_GRAPH_WIND_CTRL_SYNC (IPU_DP_BASE + 0x04)`, `DP_FG_POS_SYNC (IPU_DP_BASE + 0x08)`.
   - Factor `imx6_ipu_get_ch_addr(IMX6IPUState *s, int ch, int *bpp_out)` for both Channel 23 (`MEM_BG_SYNC`) and Channel 27 (`MEM_FG_SYNC`).
   - In `imx6_ipu_gfx_update`, if `s->regs[IPU_IDMAC_CH_EN_1 >> 2] & (1u << 27)`, DMA-read Channel 27 into `s->fg_buf`, parse `DP_COM_CONF_SYNC` (`bit 2 = GWAM` global alpha vs local alpha, `bit 1 = GWCKE` color key, `bit 0 = GWSEL` fg on top vs bg on top) and `DP_GRAPH_WIND_CTRL_SYNC` (`bits 31:24 = GWAV` global alpha, `bits 23:0 = GWCK` RGB color key), and composite onto `surface` (force full-frame redraw while overlay is active). When bit 27 is clear, execute the existing fast Channel 23 path with zero overhead.

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan3_peripherals.py -v && python3 tools/tests/test_plan4_display_performance.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/arm/rcd330.c hw/misc/rcd330_vip.c hw/display/imx6_ipu.c
git -C qemu commit -m "hw/rcd330: wire REV to GPIO2_12 (gpio44) and add IPUv3 Ch 27 foreground overlay compositing"
git add tools/arm_hal_shim/hal_gpio_shim.c tools/prebuilt/libhal_gpio_shim.so tools/tests/test_emu_plan3_peripherals.py
git commit -m "tools/arm_hal_shim: sync virtual gpio44_value from rcd330-vip live REV state"
```

---

### Task 4: USB Host Storage (`--usb <disk.img>`) & Qt6 GUI Phase 3 Controls

**Files:**
- Modify: `tools/run_rcd330.sh:15-130,240-260`
- Modify: `tools/hmi_qemu.py:48-90`
- Modify: `tools/rcd330_gui/src/vehicle_tab.h`, `tools/rcd330_gui/src/vehicle_tab.cpp`
- Modify: `tools/rcd330_gui/src/vip_client.h`, `tools/rcd330_gui/src/vip_client.cpp`
- Modify: `tools/rcd330_gui/tests/test_rcd330_gui.cpp`
- Modify: `tools/tests/test_emu_plan3_peripherals.py`

**Interfaces:**
- Consumes: `fsl-imx6.c` `TYPE_CHIPIDEA` USB host controller (`usb-bus.0`/`usb-bus.1`) + stock kernel `fsl-ehci` & `usb-storage`; `AUX`, `ECALL`, `SAFECODE`, `BACKLIGHT`, `AUDIO` VIP commands/notifications from Task 2.
- Produces:
  - `--usb <disk.img>` option in `tools/run_rcd330.sh` (and `usb_img` arg in `Rcd330Qemu`) passing `-drive if=none,id=usbstick,file=<disk.img>,format=raw -device usb-storage,drive=usbstick`.
  - Qt6 GUI toggles in `VehicleTab` for `AUX Plugged` (`AUX 1`/`0`), `eCall Mute` (`ECALL 1`/`0`), `Safe Code Lock` (`SAFECODE 1`/`0`), plus a live status readout parsing `AUDIO ...` and `BACKLIGHT ...` notifications from `VipClient` (dimming `RfbScreenWidget` visually when `BACKLIGHT 0` is reported).

- [ ] **Step 1: Add failing CLI and Qt6 GUI tests**

Append to `TestEmuPlan3Peripherals` in `tools/tests/test_emu_plan3_peripherals.py`:
```python
    def test_run_rcd330_usb_storage_flag(self):
        import subprocess
        out = subprocess.check_output(
            [str(ROOT / "tools/run_rcd330.sh"), "--headless", "--dry-run", "--usb", "/tmp/music.img"],
            text=True,
        )
        self.assertIn("id=usbstick,file=/tmp/music.img,format=raw", out)
        self.assertIn("usb-storage,drive=usbstick", out)
```
And add a test in `tools/rcd330_gui/tests/test_rcd330_gui.cpp` checking `auxBtn` (`AUX 1`/`0`), `ecallBtn` (`ECALL 1`/`0`), and `safeCodeBtn` (`SAFECODE 1`/`0`).

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 tools/tests/test_emu_plan3_peripherals.py -v && ./tools/build_gui.sh --test`
Expected: FAIL (`--usb` unknown and new GUI buttons missing).

- [ ] **Step 3: Implement `--usb` in `run_rcd330.sh` / `hmi_qemu.py` and Phase 3 controls in `tools/rcd330_gui`**

1. In `tools/run_rcd330.sh`, parse `--usb <path>` / `--usb=<path>` and when set append:
   `-drive "if=none,id=usbstick,file=$USB_IMG,format=raw" -device "usb-storage,drive=usbstick"`.
2. In `tools/hmi_qemu.py`, add optional `sd_img=None, usb_img=None` parameters to `Rcd330Qemu.__init__` and forward `--sd` / `--usb` to `RUN_SCRIPT`.
3. In `tools/rcd330_gui/src/vehicle_tab.{h,cpp}`, add checkable `auxBtn` (`AUX 1`/`0`), `ecallBtn` (`ECALL 1`/`0`), and `safeCodeBtn` (`SAFECODE 1`/`0`) buttons (reset on `resetForReboot()`).
4. In `tools/rcd330_gui/src/vip_client.{h,cpp}`, parse incoming `BACKLIGHT <0|1>` and `AUDIO ...` lines into Qt signals (`backlightChanged(bool)`, `audioStateChanged(QString)`) displayed on the status bar.

- [ ] **Step 4: Build and run GUI + Python tests**

Run: `./tools/build_gui.sh --test && python3 tools/tests/test_emu_plan3_peripherals.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/run_rcd330.sh tools/hmi_qemu.py tools/rcd330_gui/ tools/tests/test_emu_plan3_peripherals.py
git commit -m "tools: add --usb mass storage support and Phase 3 AUX/eCall/SafeCode/Backlight GUI controls"
```

---

### Task 5: Full VIP AM/FM Tuner Peer (`05:01`), `HOSTCMD`, `TUNER_TRACE` & `hmi_qemu.py` Helpers

Stock and recreated HMIs (HMI Phase 3a, `docs/superpowers/plans/2026-11-26-hmi-phase3a-tuner.md`) can only be compared on seek, scan, presets and station lists if the VIP answers the full `IPCL 0x05:0x01` command set accurately and exposes a command/reply trace. Today `rcd330_vip.c` handles only `0x65`, `0x37` step, `0x30`, and `0x4e`, and sends RDS radiotext on `0x3f` instead of the preset block.

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c`, `tools/hmi_qemu.py`, `docs/ipc-and-vgw.md`
- Create: `tools/tests/test_vip_tuner.py`

**Interfaces:**
- Produces, as VIP TCP commands:
  - `HOSTCMD <grp> <sub> <hex-payload>` feeds a host→VIP frame through the same handler as SPI (`vip_handle_host_frame`) and replies `OK HOSTCMD\n`.
  - `TUNER_TRACE 0|1` turns on asynchronous `EVT TUNERCMD <hex>\n` lines (each received `05:01` payload) and `EVT TUNER <hex>\n` lines (each `05:00` reply payload) and replies `OK TUNER_TRACE\n`.
- Produces in Python (`tools/hmi_qemu.py`):
  - `Rcd330Qemu.hostcmd(grp, sub, payload: bytes)`
  - `Rcd330Qemu.tuner_trace(on: bool)`
  - `Rcd330Qemu.tuner_events(since) -> list[tuple[str, bytes]]` (`("CMD"|"RX", payload)`). `_drain_vip` must separate/skip `EVT ` lines so that synchronous `vip()` replies stay in sync.
  - `Rcd330Qemu.drag(x0, y0, x1, y1, ...)` (QMP press-move-release drag for touch-scrollable lists such as the tuner station list `0xfb`).
- Reply contract (`IPCL 0x05:0x00` → `VAMFMProxy::RecieveVmfMsg` `0x000ae28c`):
  - `0x3b` gives `0x18` bytes.
  - `0x3d` gives a `0xe`-byte status: `[0..1]` freq LE, `[2..9]` PS, `[10]` band (`<3` FM, `3` AM), `[11..13]` flags/state.
  - `0x3e` is band changed.
  - `0x3f` is `0x48` bytes (preset block: 6 × 12-byte entries).
  - `0x40` is `[0x01, n×12]`, each 12-byte entry `[freq lo, hi, flags, ps[8], sig]`.
  - `0x42` gives `0x20` bytes.
  - `0x43` is seek mode.
- Verified `IPCL 0x05:0x01` host→VIP command bytes (from stock `VAMFMProxy` `0xad104..0xaf298`):

  | Stock VA | Operation | `05:01` payload bytes |
  |---|---|---|
  | `0xad104` | GetCurrentInfo | `3d` |
  | `0xad1a4` | TunerList | `3b 00 00` |
  | `0xad1f0` | GetStaionListInfo (`x = proxy+0x4a`) | `3c x 00` |
  | `0xad29c`/`0xad34c` | ManualSeekUp/Dn | `37 00 01` / `37 01 01` |
  | `0xad400`/`0xad4ac` | ManualRepeatSeekUp/Dn | `38 00 07` / `38 01 07` |
  | `0xad560` | Step(`s`) | `37 (s<0)\|(abs(s)<<1) 01` |
  | `0xad5d4`/`0xad680` | SeekUp/Dn | `37 00 02` / `37 01 02` |
  | `0xad738`/`0xad7e8` | SeekStop/SeekCancel | `37 02 00` / `37 03 00` |
  | `0xad88c` / `0xad8f4` | Preset/mode ops | `33 x ff` / `34 …` |
  | `0xad9b0`/`0xada68` | PrestSet/PrestCall(`i`, `0..5`) | `35 i+1 ff` / `36 i+1 ff` |
  | `0xadb2c`/`0xadbd0`/`0xadc80` | Scan/ScanStop/ScanCancle | `40 00 00` / `40 04 00` / `40 06 00` |
  | `0xadd30`/`0xadd84` | AutoStore/AutoStoreCancel | `3e 00 04` / `3e 02 00` |
  | `0xade30`/`0xade7c` | InitAutoStore/InitAutoStoreCancel | `3f 00 00` / `3f 02 00` |
  | `0xadf18` | SetFrequency(`f`) | `65 f>>8 f&ff` |
  | `0xadff0`/`0xae094` | DeletePreset(`i`)/DeletePresetAll | `4a i+1 00` / `49 00 00` |
  | `0xae128`/`0xae1f0` | SetSeekmode(`m`)/GetSeekmode | `48 {0:00,1:08,2:07} 00` / `4e 00 00` |
  | `0xaeaf4` | SetSortingSTL(`m`) | `47 {0:00,1:09,2:0a} 00` |
  | `0xaec08`/`0xaecb0` | StationOrPresetNext/Previous | `4b 00 00` / `4b 01 00` |
  | `0xaedb0` / `0xaee44` | StationlistBack / StationlistStorePreset(`band`, `i`) | `3a 00 00` / `4d band i+1` |
  | `0xaf070` | FactroyReset | `50 00 00` |
  | `0xaf164` | Band(`b`) | `30 b` |

- [ ] **Step 1: Pin the `0x3f` meaning and the state codes.** The model currently sends RDS radiotext as `0x3f`. The HMI copies those `0x48` bytes to tuner data `+0x1a` (`0x00199278`), and `VTunerMainPageImpl_::UpdatePresetsInfo` (`0x001232e4`) reads them as the preset block (6 × 12-byte entries). Read `0x001232e4`, `0x0012226c` and `0x00121ad8`, and record in `docs/ipc-and-vgw.md`: which payload carries presets, where radiotext travels (if anywhere), and the tuner state codes (seeking, scanning, auto-store, tuned; `0x3d`'s drop rule uses `0x0c`/`0x0d`). Use these findings in Step 4.

- [ ] **Step 2: Write the failing test `tools/tests/test_vip_tuner.py`** (`RCD330_QEMU_TESTS=1`). It boots the stock HMI (`Rcd330Qemu()`), waits for `wait_hmi_ready()`, then runs `tuner_trace(True)` and these checks:

```python
def test_seek_up_tunes_next_station(self):
    q.hostcmd(0x05, 0x01, bytes([0x37, 0x00, 0x02]))           # OnSeekUpOpera 0xad5d4
    rx = [p for k, p in q.tuner_events(since) if k == "RX" and p[0] == 0x3d]
    self.assertGreaterEqual(len(rx), 2)                         # seeking state, then tuned
    self.assertNotEqual(rx[-1][1:3], start_freq)                # landed on another station
def test_preset_store_recall_delete(self):     # 0x35 i+1 ff / 0x36 i+1 ff / 0x4a i+1 00 -> 0x3f entry i set / 0x3d freq / entry zeroed
def test_scan_and_cancel(self):                # 0x40 00 00 -> repeated 0x3d; 0x40 06 00 -> back to start freq, tuned
def test_autostore_emits_list_and_presets(self):   # 0x3e 00 04 -> 0x40 list, then 0x3f presets
def test_station_list_request(self):           # 0x3c x 00 -> 0x40, (len - 2) % 12 == 0
def test_seek_mode_set_get(self):              # 0x48 08 00 then 0x4e 00 00 -> 0x43 08
def test_unknown_item_is_ignored(self):        # 0x7e -> no RX; q.vip("PING") still answers
def test_trace_lines_do_not_break_vip_replies(self):   # q.vip("PING") == "PONG rcd330-vip" with trace on
```

- [ ] **Step 3: Run it to verify it fails.**
Run: `RCD330_QEMU_TESTS=1 python3 -m unittest tools.tests.test_vip_tuner -v`
Expected: FAIL (`ERR unknown command` for `HOSTCMD`).

- [ ] **Step 4: Implement the tuner peer in `qemu/hw/misc/rcd330_vip.c` and helpers in `tools/hmi_qemu.py`.**
  - Extend `Rcd330VipState` with: 6 presets per band, a seek-mode byte, a station-list sort byte, and a seek/scan/auto-store state + QEMU timer(s). *(Note for Emulator Plan 5 Task 3: include these new tuner fields/timers in `vmstate_rcd330_vip`.)*
  - Every step that moves the state sends `0x3d`. Seek moves to the next or previous `vip_fm_presets` entry (AM: 9 kHz steps, stopping at the band edge) through a QEMU timer, with "seeking" then "tuned", using Step 1's state codes. Scan repeats seek with a 5 s dwell until stop (`04`) or cancel (`06`). Auto-store builds the list, fills presets from the 6 strongest entries, and sends `0x40` then `0x3f`.
  - Fix `0x3f` to send the `0x48`-byte preset block per Step 1.
  - Handle all commands in the verified `05:01` table above (`0x30`, `0x35`, `0x36`, `0x37`, `0x38`, `0x3a`, `0x3b`, `0x3c`, `0x3d`, `0x3e`, `0x3f`, `0x40`, `0x47`, `0x48`, `0x49`, `0x4a`, `0x4b`, `0x4d`, `0x4e`, `0x50`, `0x65`); ignore unknown items cleanly.
  - Add `HOSTCMD` and `TUNER_TRACE` to `vip_process_cmd` next to the existing `TUNER `.
  - Add `hostcmd()`, `tuner_trace()`, `tuner_events()`, `EVT `-aware `_drain_vip`, and `drag()` to `tools/hmi_qemu.py`.

- [ ] **Step 5: Run the tests to verify they pass**, then rebuild QEMU (`./tools/build_qemu.sh`) and run `test_vip_tuner` plus the Phase 2 QEMU suites (`test_hmi_boot`, `test_hmi_parity`, `test_hmi_interactive`). Expected: all PASS. The boot reply sequence is unchanged apart from Step 1's `0x3f` fix.

- [ ] **Step 6: Commit** (inside `qemu/` first, then the parent with the submodule pointer, `tools/hmi_qemu.py`, the test and `docs/ipc-and-vgw.md`):

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: full AM/FM tuner peer (seek, scan, presets, auto-store, HOSTCMD, TUNER_TRACE)"
git add qemu tools/hmi_qemu.py tools/tests/test_vip_tuner.py docs/ipc-and-vgw.md
git commit -m "qemu/tools: rcd330 VIP AM/FM tuner peer, trace/drag helpers, and VIP tuner tests"
```
