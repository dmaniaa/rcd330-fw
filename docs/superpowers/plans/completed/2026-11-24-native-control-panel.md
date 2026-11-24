# Native Unified Qt6 Control Panel & Hardware Long-Press Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `tools/rcd330_panel.py` with a cross-platform (macOS + Linux) native Qt6 Widgets application (`tools/rcd330_gui/`) embedding QEMU's 800×480 touchscreen inside a unified VW RCD 330 faceplate window with true hardware button press-and-hold / quick-hold long-press support, `POWER` 10 s cold reboot, and non-volatile VIP coding EEPROM persistence.

**Architecture:** `tools/run_rcd330.sh` starts QEMU headless with local RFB/VNC (`-display none -vnc 127.0.0.1:<port>`) and the existing VIP TCP chardev (`127.0.0.1:4444`), then runs `build/gui/rcd330_gui` in the foreground so closing the window cleanly shuts down QEMU. `rcd330_gui` embeds a lightweight RFB 3.8 client (`Raw` + `CopyRect` + `DesktopSize`, 32bpp little-endian XRGB directly into `QImage::Format_RGB32`), hold-aware physical bezel buttons, rotary wheel knobs, and a collapsible tabbed dock (Vehicle CAN/BAP & Radio, Headunit Coding delegating to `tools/rcd330_coding.py` JSON CLI, and VIP Log).

**Tech Stack:** C++20, Qt6 (`Qt6::Widgets`, `Qt6::Network`, `Qt6::Test`), CMake + Ninja/Make, C (QEMU 11 device models `rcd330_vip.c` & `imx6_ipu.c`), Python 3 stdlib (`tools/rcd330_coding.py`, `unittest`), Bash (`tools/build_gui.sh`, `tools/run_rcd330.sh`).

**Spec:** [docs/superpowers/specs/2026-11-24-native-control-panel-design.md](../../specs/2026-11-24-native-control-panel-design.md)

## Global Constraints

- Cross-platform host target: macOS and Linux using only `Qt6::Widgets` and `Qt6::Network` (no `Qt6::WebEngineWidgets` / Chromium).
- Remove `tools/rcd330_panel.py` completely; no browser HTTP server fallback.
- Single source of truth for VW Long Coding (`tools/rcd330_coding.py`): all bitfield encoding/decoding, `vipImage.bin 0x1f48a` validation, JSON persistence, and VIP command generation live exclusively in `tools/rcd330_coding.py`.
- Exact firmware hardkey hold thresholds (`5624_vw` `opt/hmi` `0x3c564` / `0x42088`): `2.0 s` Long Press, `4.0 s` Extended Long / Service Mode, `9.0 s` Ultra-Long / Engineering Testmode; `POWER` knob: `< 10 s` release = standby toggle, `10.0 s` continuous hold = `qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)`.
- Never commit `build/`, `original-fw/`, or `extracted-fw/`; keep `qemu/` submodule and root repo commits clean and synchronized.

## Review Focus

1. **Interleaved async `HOST_TX` / `REBOOT` lines & fragmented TCP reads on VIP socket:** QEMU emits `HOST_TX ...` and `REBOOT POWER_10S` asynchronously on port `4444` and TCP can split replies mid-line; `VipClient` must buffer until `\n`, route `HOST_TX`/`REBOOT` lines to the log without consuming them as the reply to a pending command, and reject commands containing `\r`/`\n` (pinned by `TestRcd330Gui::testVipClientProtocolHardening` in Task 4).
2. **Fragmented / multi-rect RFB frames & overlapping `CopyRect`:** TCP splits 800×480 `Raw` pixel payloads across multiple `readyRead()` deliveries; `RfbSession` must accumulate exact byte counts without desyncing, handle multi-rect updates, and copy overlapping `CopyRect` regions safely via temporary copy (pinned by `TestRcd330Gui::testRfbHandshakeFragmentedRawAndCopyRect` in Task 3).
3. **Stuck physical keys on focus loss, window hide, or TCP disconnect:** Holding `SETUP` or `POWER` and Alt-Tabbing, closing the window, or killing the GUI must never leave keys stuck down; `BezelButton` must emit `BTN <KEY> 0` on `focusOutEvent`/`hideEvent`/destruction and `rcd330_vip.c` must release all `held_keys_mask` keys and cancel `pwr_hold_timer` on `CHR_EVENT_CLOSED` (pinned by `TestRcd330Gui::testBezelButtonLongPressAndSafetyGuards` in Task 4 and `test_vip_power_long_press_reboot_and_eeprom_persistence` in Task 1).
4. **Mid-hold early cancel & `POWER` double-press / post-reboot release races:** Cancelling a scripted quick-hold early or sending duplicate `BTN POWER 1` or releasing `POWER` after the 10 s reboot already fired must never emit duplicate presses or accidentally flip standby after reboot (pinned by `TestRcd330Gui::testBezelButtonLongPressAndSafetyGuards` in Task 4 and `test_vip_power_long_press_reboot_and_eeprom_persistence` in Task 1).
5. **Scaled / HiDPI touchscreen coordinate mapping & drag-off-canvas clamping:** Pointer events near screen borders or dragged outside `RfbScreenWidget` must clamp strictly to `[0..799, 0..479]` independent of scaling or Retina `devicePixelRatio`, and releasing outside must emit `mask=0` (pinned by `TestRcd330Gui::testRfbPointerCoordinateClamping` in Task 3).

---

## File Map

| Path | Action | Responsibility |
|---|---|---|
| `qemu/hw/misc/rcd330_vip.c` | Modify | `POWER` 10 s hold timer + `qemu_system_reset_request`, `held_keys_mask` + `CHR_EVENT_CLOSED` cleanup, move EEPROM defaults to `vip_realize` so coding survives reboot |
| `qemu/hw/display/imx6_ipu.c` | Modify | Zero `DisplaySurface` & emit `qemu_console_update` inside `imx6_ipu_reset` so reboot visibly blanks screen |
| `tools/rcd330_coding.py` | Modify | Add `load`, `apply`, and `reset` JSON CLI subcommands returning normalized config, decoded fields, lookup maps, and VIP commands |
| `tools/build_gui.sh` | Create | Cross-platform (macOS Homebrew Qt6 + Linux system Qt6) CMake build + `--test` headless `ctest` runner staging `build/gui/rcd330_gui` |
| `tools/rcd330_gui/CMakeLists.txt` | Create | C++20 Qt6 build for `rcd330_gui_core`, `rcd330_gui`, and `test_rcd330_gui` |
| `tools/rcd330_gui/src/rfb_protocol.{h,cpp}` | Create | Pure RFB 3.8 protocol state machine (`Raw`, `CopyRect`, `DesktopSize`, 32bpp XRGB → `QImage::Format_RGB32`) |
| `tools/rcd330_gui/src/rfb_widget.{h,cpp}` | Create | Embedded 800×480 live touchscreen `QWidget` with `QTcpSocket` auto-reconnect and 1:1 clamped touch `PointerEvent`s |
| `tools/rcd330_gui/src/vip_client.{h,cpp}` | Create | Async reconnecting VIP TCP client (`127.0.0.1:4444`), newline hardening, out-of-band `HOST_TX`/`REBOOT` routing, `PING` keepalive, startup coding sync |
| `tools/rcd330_gui/src/coding_bridge.{h,cpp}` | Create | `QProcess` JSON bridge to `tools/rcd330_coding.py` (`load`, `apply`, `reset`) |
| `tools/rcd330_gui/src/bezel_button.{h,cpp}` | Create | Hold-aware virtual hardware button with live elapsed timer/progress bar, milestone colors, right-click quick-hold menu, early cancel, and safety auto-release |
| `tools/rcd330_gui/src/rotary_knob_widget.{h,cpp}` | Create | Rotary knob cluster (`-` / center `BezelButton` / `+`) with mouse-wheel encoder stepping |
| `tools/rcd330_gui/src/vehicle_tab.{h,cpp}` | Create | CAN/BAP vehicle telemetry, Ignition clamp, Reverse/RVC, OPS, Doors, Clock sync, MFSW SWC, FM/RDS radio presets |
| `tools/rcd330_gui/src/coding_tab.{h,cpp}` | Create | Full PQ (7B) & MQB (25B) Headunit Coding editor, PQ↔MQB switching, raw hex, live validation warnings, Save/Apply/Reset |
| `tools/rcd330_gui/src/log_tab.{h,cpp}` | Create | Scrolling monospace VIP/IPCL/CAN log viewer, Clear button, and raw command input bar |
| `tools/rcd330_gui/src/main_window.{h,cpp}` | Create | Unified VW RCD 330 faceplate + collapsible tabbed dock + QEMU PID liveness monitor |
| `tools/rcd330_gui/src/main.cpp` | Create | CLI parser, automotive dark QSS theme, `MainWindow` entrypoint |
| `tools/rcd330_gui/tests/test_rcd330_gui.cpp` | Create | Headless Qt Test suite (`QT_QPA_PLATFORM=offscreen`) |
| `tools/run_rcd330.sh` | Modify | Auto-build `build/gui/rcd330_gui`, launch QEMU with `-display none -vnc 127.0.0.1:<port>`, run `rcd330_gui` in foreground with QEMU cleanup trap |
| `tools/rcd330_panel.py` | Delete | Remove obsolete browser HTTP control panel |
| `tools/tests/test_bugfixes_and_coding.py` | Modify | Replace HTTP panel tests with `rcd330_coding.py` JSON CLI, VIP reboot/EEPROM/disconnect, IPU blank-on-reset, and `run_rcd330.sh` GUI integration tests |
| `tools/tests/test_plan3_rcs_boot_and_tuner.py` | Modify | Replace `rcd330_panel` import test with verification of `rcd330_gui` tuner controls and `VipClient` hardening |
| `AGENTS.md`, `README.md` | Modify | Document native Qt6 window, `tools/build_gui.sh`, Qt6 host prerequisites, and hardware button long-press support |

---

### Task 1: QEMU VIP `POWER` 10 s Hold/Reboot, Disconnect Key Release, EEPROM Persistence & IPU Blank-on-Reset

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c:15-138`, `qemu/hw/misc/rcd330_vip.c:943-966`, `qemu/hw/misc/rcd330_vip.c:1200-1394`
- Modify: `qemu/hw/display/imx6_ipu.c:325-332`
- Test: `tools/tests/test_bugfixes_and_coding.py`

**Interfaces:**
- Consumes: QEMU 11 `#include "system/runstate.h"` (`qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)`), `QEMU_CLOCK_REALTIME`, `qemu_chr_fe_set_handlers` `event_handler` (`CHR_EVENT_CLOSED`).
- Produces:
  - `BTN POWER 1` arms 10,000 ms realtime timer; firing emits `REBOOT POWER_10S\n` and calls `qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)`.
  - `BTN POWER 0` cancels timer and toggles `s->pwr_off` only if pressed and reboot has not fired.
  - `CHR_EVENT_CLOSED` cancels `pwr_hold_timer` and enqueues key-up (`value=0`) for all bits in `s->held_keys_mask`.
  - Non-volatile EEPROM fields (`platform`, `brand`, `vehicle`, `can_speed`, `part_num`, `hw_ver`, `serial_num`, `coding`, `coding_len`, `speakers`, `eng_mode`, `ops_sensors`) initialized once in `vip_realize` and preserved across `vip_reset_hold`.
  - `imx6_ipu_reset` blanks `DisplaySurface` to black and calls `qemu_console_update`.

- [ ] **Step 1: Write the failing test in `tools/tests/test_bugfixes_and_coding.py`**

Add a test method inside `TestBugfixesAndCoding` in `tools/tests/test_bugfixes_and_coding.py`:

```python
    # --- Native Panel Support: POWER 10s Reboot, Disconnect Safety, EEPROM & Blank-on-Reset ---
    def test_vip_power_long_press_reboot_and_eeprom_persistence(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        ipu_c = (ROOT / "qemu/hw/display/imx6_ipu.c").read_text()

        # POWER 10s hold timer + cold reboot via qemu_system_reset_request
        self.assertIn('#include "system/runstate.h"', vip_c)
        self.assertIn("pwr_hold_timer", vip_c)
        self.assertIn("vip_pwr_hold_timer_cb", vip_c)
        self.assertIn("QEMU_CLOCK_REALTIME", vip_c)
        self.assertIn("qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)", vip_c)
        self.assertIn("REBOOT POWER_10S", vip_c)
        self.assertIn("pwr_btn_down", vip_c)
        self.assertIn("pwr_reboot_fired", vip_c)

        # Stuck-key & power-timer cleanup on TCP disconnect
        self.assertIn("held_keys_mask", vip_c)
        self.assertIn("vip_chr_event", vip_c)
        self.assertIn("CHR_EVENT_CLOSED", vip_c)

        # Non-volatile coding/VRM EEPROM initialized in vip_realize, preserved across vip_reset_hold
        realize_fn = vip_c.split("static void vip_realize(")[1].split("\n}")[0]
        reset_fn = vip_c.split("static void vip_reset_hold(")[1].split("\n}")[0]
        self.assertIn("default_pq_coding", realize_fn)
        self.assertIn("s->platform = 1;", realize_fn)
        self.assertNotIn("default_pq_coding", reset_fn)
        self.assertNotIn("s->coding_len =", reset_fn)
        self.assertIn("timer_del(s->pwr_hold_timer);", reset_fn)
        self.assertIn("s->held_keys_mask = 0;", reset_fn)

        # IPU blanks display surface on reset so 10s POWER reboot shows immediate black screen
        ipu_reset = ipu_c.split("static void imx6_ipu_reset(")[1].split("\n}")[0]
        self.assertIn("qemu_console_surface", ipu_reset)
        self.assertIn("qemu_console_update", ipu_reset)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tools.tests.test_bugfixes_and_coding.TestBugfixesAndCoding.test_vip_power_long_press_reboot_and_eeprom_persistence -v`
Expected: FAIL (`AssertionError: '#include "system/runstate.h"' not found`).

- [ ] **Step 3: Implement `rcd330_vip.c` and `imx6_ipu.c` changes**

In `qemu/hw/misc/rcd330_vip.c`:
1. Add `#include "system/runstate.h"` right after `#include "system/memory.h"`.
2. Add fields to `struct Rcd330VipState`:
```c
    QEMUTimer *pwr_hold_timer;
    bool pwr_btn_down;
    bool pwr_reboot_fired;
    uint16_t held_keys_mask;
```
3. Add `vip_pwr_hold_timer_cb` right above `vip_process_cmd`:
```c
static void vip_pwr_hold_timer_cb(void *opaque)
{
    Rcd330VipState *s = RCD330_VIP(opaque);
    if (!s->pwr_btn_down) {
        return;
    }
    s->pwr_reboot_fired = true;
    s->pwr_btn_down = false;
    vip_notify_host(s, "REBOOT POWER_10S\n");
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}
```
4. Update `BTN` handling in `vip_process_cmd`:
```c
    if (sscanf(line, "BTN %63s %d", arg1, &a) == 2) {
        if (!strcmp(arg1, "POWER")) {
            if (a) {
                if (!s->pwr_btn_down) {
                    s->pwr_btn_down = true;
                    s->pwr_reboot_fired = false;
                    timer_mod(s->pwr_hold_timer,
                              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 10000);
                }
            } else {
                timer_del(s->pwr_hold_timer);
                if (s->pwr_btn_down && !s->pwr_reboot_fired) {
                    s->pwr_off = !s->pwr_off;
                    vip_send_power_state(s);
                }
                s->pwr_btn_down = false;
                s->pwr_reboot_fired = false;
            }
        } else if (!strcmp(arg1, "MUTE")) {
            if (a) {
                vip_send_swc_key(s, 0x5d);
            }
        } else {
            uint8_t code = vip_button_code(s, arg1);
            if (code > 0 && code < 16) {
                uint8_t val = (uint8_t)(a ? 1 : 0);
                if (val) {
                    s->held_keys_mask |= (uint16_t)(1u << code);
                } else {
                    s->held_keys_mask &= (uint16_t)~(1u << code);
                }
                vip_enqueue_key(s, code, val);
            }
        }
        vip_notify_host(s, "OK BTN\n");
    }
```
5. Add `vip_chr_event` right after `vip_chr_receive`:
```c
static void vip_chr_event(void *opaque, QEMUChrEvent event)
{
    Rcd330VipState *s = RCD330_VIP(opaque);
    if (event == CHR_EVENT_CLOSED) {
        timer_del(s->pwr_hold_timer);
        s->pwr_btn_down = false;
        s->pwr_reboot_fired = false;
        if (s->held_keys_mask != 0) {
            for (uint16_t code = 1; code < 16; code++) {
                if (s->held_keys_mask & (1u << code)) {
                    vip_enqueue_key(s, code, 0);
                }
            }
            s->held_keys_mask = 0;
        }
    }
}
```
6. Update `vip_realize` to initialize `pwr_hold_timer`, register `vip_chr_event`, and initialize the non-volatile EEPROM defaults once:
```c
static void vip_realize(SSIPeripheral *dev, Error **errp)
{
    Rcd330VipState *s = RCD330_VIP(dev);
    static const uint8_t default_pq_coding[7] = {
        0x0e, 0x00, 0x04, 0x40, 0x04, 0x00, 0x13
    };

    qdev_init_gpio_out_named(DEVICE(dev), &s->srq_irq, "srq", 1);
    qdev_init_gpio_out_named(DEVICE(dev), &s->knob_a_irq, "knob-a", 1);
    qdev_init_gpio_out_named(DEVICE(dev), &s->knob_b_irq, "knob-b", 1);
    s->srq_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vip_srq_timer_cb, s);
    s->knob_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vip_knob_timer_cb, s);
    s->pwr_hold_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                     vip_pwr_hold_timer_cb, s);

    /* Non-volatile RH850 VIP EEPROM defaults (preserved across SoC resets) */
    s->platform = 1;  /* PQ */
    s->brand = 2;     /* VW */
    s->vehicle = 0x13;/* VW370 */
    s->can_speed = 1; /* HS 500k */
    pstrcpy(s->part_num, sizeof(s->part_num), "6RD035187B ");
    pstrcpy(s->hw_ver, sizeof(s->hw_ver), "541");
    pstrcpy(s->serial_num, sizeof(s->serial_num), "VWZ1Z2R3300001");
    memset(s->coding, 0, sizeof(s->coding));
    memcpy(s->coding, default_pq_coding, sizeof(default_pq_coding));
    s->coding_len = sizeof(default_pq_coding);
    s->speakers = 4;
    s->eng_mode = 0;
    s->ops_sensors[0] = 4;
    s->ops_sensors[1] = 4;
    s->ops_sensors[2] = 0;
    s->ops_sensors[3] = 0;

    memory_region_init_io(&s->mmio, OBJECT(dev), &vip_mmio_ops, s,
                          "rcd330-vip-mmio", 0x1000);
    qemu_chr_fe_set_handlers(&s->chr, vip_chr_can_receive, vip_chr_receive,
                             vip_chr_event, NULL, s, NULL, true);
}
```
7. Update `vip_reset_hold` to delete `pwr_hold_timer`, clear `pwr_btn_down`, `pwr_reboot_fired`, `held_keys_mask`, and NOT overwrite the non-volatile EEPROM fields (`platform`, `brand`, `vehicle`, `can_speed`, `part_num`, `hw_ver`, `serial_num`, `coding`, `coding_len`, `speakers`, `eng_mode`, `ops_sensors`).
8. Update `imx6_ipu_reset` in `qemu/hw/display/imx6_ipu.c`:
```c
static void imx6_ipu_reset(DeviceState *dev)
{
    IMX6IPUState *s = IMX6_IPU(dev);
    memset(s->regs, 0, IMX6_IPU_SIZE);
    s->prev_bpp = 0;
    s->have_prev_fb = false;
    if (s->con) {
        DisplaySurface *surface = qemu_console_surface(s->con);
        if (surface) {
            memset(surface_data(surface), 0,
                   (size_t)surface_stride(surface) * surface_height(surface));
            qemu_console_update(s->con, 0, 0,
                                surface_width(surface),
                                surface_height(surface));
        }
    }
}
```

- [ ] **Step 4: Build QEMU and run tests to verify they pass**

Run: `./tools/build_qemu.sh && python3 -m unittest discover -s tools/tests -v`
Expected: `qemu-system-arm` builds cleanly with zero warnings/errors and all Python tests PASS.

- [ ] **Step 5: Commit QEMU submodule changes**

```bash
git -C qemu add hw/misc/rcd330_vip.c hw/display/imx6_ipu.c
git -C qemu commit -m "rcd330: add POWER 10s hold reboot, disconnect key cleanup, EEPROM reset persistence, and IPU blank-on-reset"
```

---

### Task 2: `tools/rcd330_coding.py` JSON CLI Subcommands (`load`, `apply`, `reset`)

**Files:**
- Modify: `tools/rcd330_coding.py:530-551`
- Test: `tools/tests/test_bugfixes_and_coding.py`

**Interfaces:**
- Consumes: Existing `default_coding_config`, `load_coding_config`, `save_coding_config`, `apply_coding_update`, `decode_coding_fields`, `vip_commands_for_coding`, `PQ_VEHICLES`, `MQB_VEHICLES`, `MQB_BOOTLOGOS` in `tools/rcd330_coding.py`.
- Produces:
  - `build_coding_payload(cfg, coding_path, apply_live)` returning JSON-serializable dict with keys `config`, `decoded`, `vip_cmds`, `coding_file`, `pq_vehicles`, `mqb_vehicles`, `mqb_bootlogos`.
  - CLI subcommands:
    - `python3 tools/rcd330_coding.py load --coding-file <path> --fw-id <id>`
    - `python3 tools/rcd330_coding.py apply --coding-file <path> --fw-id <id> [--live]` (reads JSON update dict from stdin)
    - `python3 tools/rcd330_coding.py reset --coding-file <path> --fw-id <id> [--live]`

- [ ] **Step 1: Write the failing test in `tools/tests/test_bugfixes_and_coding.py`**

Add a test method inside `TestBugfixesAndCoding`:

```python
    def test_coding_cli_json_load_apply_and_reset(self):
        import subprocess

        script = str(ROOT / "tools/rcd330_coding.py")
        with tempfile.TemporaryDirectory() as tmp:
            coding_file = pathlib.Path(tmp) / "test_coding.json"

            # 1. load creates default payload with lookup tables & CODING_SYNC
            res_load = subprocess.run(
                [sys.executable, script, "load", "--coding-file", str(coding_file), "--fw-id", "5624_VW"],
                capture_output=True, text=True, check=True,
            )
            data_load = json.loads(res_load.stdout)
            self.assertEqual(data_load["config"]["platform"], "PQ")
            self.assertEqual(data_load["config"]["brand"], "VW")
            self.assertTrue(data_load["decoded"]["rvc_enabled"])
            self.assertIn("19", data_load["pq_vehicles"])
            self.assertIn("56", data_load["mqb_vehicles"])
            self.assertIn("24", data_load["mqb_bootlogos"])
            self.assertTrue(any(c.startswith("CODING_SYNC ") for c in data_load["vip_cmds"]))

            # 2. apply --live updates fields, writes persistent JSON, returns CODING (live)
            upd = json.dumps({
                "rvc_enabled": False,
                "ops_mode": "360",
                "speakers": 2,
                "eng_mode": True,
            })
            res_apply = subprocess.run(
                [sys.executable, script, "apply", "--coding-file", str(coding_file), "--fw-id", "5624_VW", "--live"],
                input=upd, capture_output=True, text=True, check=True,
            )
            data_apply = json.loads(res_apply.stdout)
            self.assertFalse(data_apply["decoded"]["rvc_enabled"])
            self.assertEqual(data_apply["config"]["ops_mode"], "360")
            self.assertEqual(data_apply["config"]["speakers"], 2)
            self.assertTrue(data_apply["config"]["eng_mode"])
            self.assertTrue(coding_file.is_file())
            self.assertTrue(any(c.startswith("CODING ") for c in data_apply["vip_cmds"]))
            self.assertIn("OPS_CFG 4 4 4 4", data_apply["vip_cmds"])
            self.assertIn("ENGMODE 1", data_apply["vip_cmds"])

            # 3. apply invalid raw hex returns validation_warnings without crashing
            bad_upd = json.dumps({"platform": "PQ", "coding_hex": "0E000441040113"})
            res_bad = subprocess.run(
                [sys.executable, script, "apply", "--coding-file", str(coding_file), "--fw-id", "5624_VW", "--live"],
                input=bad_upd, capture_output=True, text=True, check=True,
            )
            data_bad = json.loads(res_bad.stdout)
            self.assertGreater(len(data_bad["decoded"]["validation_warnings"]), 0)

            # 4. reset restores defaults & writes file
            res_reset = subprocess.run(
                [sys.executable, script, "reset", "--coding-file", str(coding_file), "--fw-id", "5624_VW", "--live"],
                capture_output=True, text=True, check=True,
            )
            data_reset = json.loads(res_reset.stdout)
            self.assertTrue(data_reset["decoded"]["rvc_enabled"])
            self.assertEqual(data_reset["config"]["ops_mode"], "8K")
            self.assertFalse(data_reset["config"]["eng_mode"])
            self.assertEqual(data_reset["decoded"]["validation_warnings"], [])
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tools.tests.test_bugfixes_and_coding.TestBugfixesAndCoding.test_coding_cli_json_load_apply_and_reset -v`
Expected: FAIL (`invalid choice: 'load'`).

- [ ] **Step 3: Implement `build_coding_payload` and `load`/`apply`/`reset` CLI subcommands in `tools/rcd330_coding.py`**

Add `import sys` at top of `tools/rcd330_coding.py` and update `main()` + add `build_coding_payload()`:

```python
def build_coding_payload(
    cfg: dict[str, Any],
    coding_file: str | pathlib.Path = "",
    apply_live: bool = False,
) -> dict[str, Any]:
    norm = normalize_coding_config(cfg)
    return {
        "config": norm,
        "decoded": decode_coding_fields(norm),
        "vip_cmds": vip_commands_for_coding(norm, apply_live=apply_live),
        "coding_file": str(coding_file),
        "pq_vehicles": {str(k): v for k, v in PQ_VEHICLES.items()},
        "mqb_vehicles": {str(k): v for k, v in MQB_VEHICLES.items()},
        "mqb_bootlogos": {str(k): v for k, v in MQB_BOOTLOGOS.items()},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="RCD 330 Headunit Coding Utility")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_seed = sub.add_parser("seed", help="Pre-seed /pss1 & /pss2 coding files in a staged rootfs")
    p_seed.add_argument("--stage-dir", required=True)
    p_seed.add_argument("--coding-file", default="")
    p_seed.add_argument("--fw-id", default="")

    p_load = sub.add_parser("load", help="Load coding JSON and emit structured payload")
    p_load.add_argument("--coding-file", default="")
    p_load.add_argument("--fw-id", default="")

    p_apply = sub.add_parser("apply", help="Apply JSON update from stdin, persist, and emit payload")
    p_apply.add_argument("--coding-file", required=True)
    p_apply.add_argument("--fw-id", default="")
    p_apply.add_argument("--live", action="store_true")

    p_reset = sub.add_parser("reset", help="Reset coding to firmware defaults, persist, and emit payload")
    p_reset.add_argument("--coding-file", required=True)
    p_reset.add_argument("--fw-id", default="")
    p_reset.add_argument("--live", action="store_true")

    args = ap.parse_args()
    if args.cmd == "seed":
        cfg = load_coding_config(args.coding_file or None, args.fw_id)
        seed_rootfs_coding(args.stage_dir, cfg)
        return 0
    if args.cmd == "load":
        cfg = load_coding_config(args.coding_file or None, args.fw_id)
        print(json.dumps(build_coding_payload(cfg, args.coding_file, apply_live=False)))
        return 0
    if args.cmd == "apply":
        raw_in = sys.stdin.read().strip()
        try:
            update = json.loads(raw_in) if raw_in else {}
        except json.JSONDecodeError as exc:
            print(f"Invalid JSON input: {exc}", file=sys.stderr)
            return 2
        if not isinstance(update, dict):
            print("JSON object expected", file=sys.stderr)
            return 2
        cur = load_coding_config(args.coding_file, args.fw_id)
        updated = apply_coding_update(cur, update, args.fw_id)
        saved = save_coding_config(args.coding_file, updated, args.fw_id)
        print(json.dumps(build_coding_payload(saved, args.coding_file, apply_live=args.live)))
        return 0
    if args.cmd == "reset":
        cfg = default_coding_config(args.fw_id)
        saved = save_coding_config(args.coding_file, cfg, args.fw_id)
        print(json.dumps(build_coding_payload(saved, args.coding_file, apply_live=args.live)))
        return 0
    return 1
```

Also update the docstring at top of `tools/rcd330_coding.py` from `tools/rcd330_panel.py` to `tools/rcd330_gui` so comments stay accurate.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 -m unittest tools.tests.test_bugfixes_and_coding.TestBugfixesAndCoding.test_coding_cli_json_load_apply_and_reset -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/rcd330_coding.py tools/tests/test_bugfixes_and_coding.py
git commit -m "coding: add JSON CLI subcommands (load, apply, reset) for native GUI bridge"
```

---

### Task 3: Qt6 Project Scaffolding, `tools/build_gui.sh`, RFB 3.8 Protocol & `RfbScreenWidget`

**Files:**
- Create: `tools/build_gui.sh`
- Create: `tools/rcd330_gui/CMakeLists.txt`
- Create: `tools/rcd330_gui/src/rfb_protocol.h`
- Create: `tools/rcd330_gui/src/rfb_protocol.cpp`
- Create: `tools/rcd330_gui/src/rfb_widget.h`
- Create: `tools/rcd330_gui/src/rfb_widget.cpp`
- Create: `tools/rcd330_gui/tests/test_rcd330_gui.cpp`

**Interfaces:**
- Consumes: `Qt6::Widgets`, `Qt6::Network`, `Qt6::Test`.
- Produces:
  - `class RfbSession`:
    - `enum class State { AwaitingVersion, AwaitingSecurityTypes, AwaitingSecurityResult, AwaitingServerInit, Ready, Error };`
    - `void reset();`
    - `bool feed(const QByteArray &chunk, QByteArray &outToServer, bool &frameDirty, QString &errorOut);`
    - `static QByteArray buildPointerEvent(uint8_t buttonMask, uint16_t x, uint16_t y);`
    - `static QByteArray buildFramebufferUpdateRequest(bool incremental, uint16_t x, uint16_t y, uint16_t w, uint16_t h);`
    - `const QImage &framebuffer() const;`
    - `State state() const;`
  - `class RfbScreenWidget : public QWidget`:
    - `explicit RfbScreenWidget(QWidget *parent = nullptr);`
    - `void connectToServer(const QString &host, quint16 port);`
    - `void disconnectFromServer();`
    - `bool isConnected() const;`
    - `QPoint mapWidgetToFramebuffer(const QPointF &pos) const;`
    - Signals: `void connectionChanged(bool connected);`

- [ ] **Step 1: Create `tools/rcd330_gui/CMakeLists.txt`, `tools/build_gui.sh`, minimal header stubs, and the failing RFB unit tests**

Create `tools/rcd330_gui/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(rcd330_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_library(rcd330_gui_core STATIC
    src/rfb_protocol.h
    src/rfb_protocol.cpp
    src/rfb_widget.h
    src/rfb_widget.cpp
)
target_include_directories(rcd330_gui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rcd330_gui_core PUBLIC Qt6::Widgets Qt6::Network)

option(BUILD_TESTING "Build Qt6 unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_rcd330_gui tests/test_rcd330_gui.cpp)
    target_link_libraries(test_rcd330_gui PRIVATE rcd330_gui_core Qt6::Test)
    add_test(NAME test_rcd330_gui COMMAND test_rcd330_gui)
    set_tests_properties(test_rcd330_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
```

Create `tools/build_gui.sh` (chmod +x):

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/tools/rcd330_gui"
BUILD_DIR="$ROOT_DIR/build/gui"
RUN_TESTS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            RUN_TESTS=1
            shift
            ;;
        *)
            echo "Usage: $0 [--test]" >&2
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake not found. Install cmake + Qt6 (macOS: 'brew install qt cmake', Debian/Ubuntu: 'sudo apt install qt6-base-dev cmake g++')." >&2
    exit 1
fi

CMAKE_EXTRA=()
if command -v ninja >/dev/null 2>&1; then
    CMAKE_EXTRA+=(-G Ninja)
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
    QT_PREFIX=""
    if command -v brew >/dev/null 2>&1; then
        QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
    fi
    if [[ -z "$QT_PREFIX" ]]; then
        for cand in /opt/homebrew/opt/qt /usr/local/opt/qt; do
            if [[ -d "$cand" ]]; then
                QT_PREFIX="$cand"
                break
            fi
        done
    fi
    if [[ -n "$QT_PREFIX" ]]; then
        CMAKE_EXTRA+=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
    fi
fi

mkdir -p "$BUILD_DIR"
if ! cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${CMAKE_EXTRA[@]}"; then
    echo "Error: Qt6 configuration failed. Install Qt6 Widgets/Network (macOS: 'brew install qt cmake', Debian/Ubuntu: 'sudo apt install qt6-base-dev cmake g++')." >&2
    exit 1
fi

cmake --build "$BUILD_DIR" --parallel

if [[ "$RUN_TESTS" -eq 1 ]]; then
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
```

Create `tools/rcd330_gui/tests/test_rcd330_gui.cpp` with failing RFB tests (covering Review Focus #2 and #5):

```cpp
#include <QtTest/QtTest>
#include <QtEndian>
#include "rfb_protocol.h"
#include "rfb_widget.h"

static void appendU16BE(QByteArray &buf, quint16 v) {
    char b[2];
    qToBigEndian(v, b);
    buf.append(b, 2);
}

static void appendU32BE(QByteArray &buf, quint32 v) {
    char b[4];
    qToBigEndian(v, b);
    buf.append(b, 4);
}

static void appendS32BE(QByteArray &buf, qint32 v) {
    appendU32BE(buf, static_cast<quint32>(v));
}

class TestRcd330Gui : public QObject {
    Q_OBJECT

private slots:
    void testRfbHandshakeFragmentedRawAndCopyRect() {
        RfbSession s;
        QByteArray out;
        bool dirty = false;
        QString err;

        // 1. Server version -> Client replies RFB 003.008\n
        QVERIFY(s.feed("RFB 003.008\n", out, dirty, err));
        QCOMPARE(out, QByteArray("RFB 003.008\n"));
        QCOMPARE(s.state(), RfbSession::State::AwaitingSecurityTypes);

        // 2. Security types: [count=1, type=1 (None)] -> Client picks 1
        out.clear();
        QByteArray secTypes;
        secTypes.append(char(1));
        secTypes.append(char(1));
        QVERIFY(s.feed(secTypes, out, dirty, err));
        QCOMPARE(out, QByteArray(1, char(1)));
        QCOMPARE(s.state(), RfbSession::State::AwaitingSecurityResult);

        // 3. SecurityResult = 0 -> Client sends ClientInit(shared=1)
        out.clear();
        QVERIFY(s.feed(QByteArray(4, '\0'), out, dirty, err));
        QCOMPARE(out, QByteArray(1, char(1)));
        QCOMPARE(s.state(), RfbSession::State::AwaitingServerInit);

        // 4. ServerInit (8x4, name "QEMU")
        QByteArray init;
        appendU16BE(init, 8);
        appendU16BE(init, 4);
        init.append(QByteArray(16, '\0')); // server pixel format
        appendU32BE(init, 4);
        init.append("QEMU");
        out.clear();
        QVERIFY(s.feed(init, out, dirty, err));
        QCOMPARE(s.state(), RfbSession::State::Ready);
        QCOMPARE(s.framebuffer().size(), QSize(8, 4));
        // Must emit SetPixelFormat (20) + SetEncodings (4 + 3*4 = 16) + FramebufferUpdateRequest (10) = 46 bytes
        QCOMPARE(out.size(), 46);
        QCOMPARE(quint8(out[0]), quint8(0));  // SetPixelFormat
        QCOMPARE(quint8(out[20]), quint8(2)); // SetEncodings
        QCOMPARE(quint8(out[36]), quint8(3)); // FramebufferUpdateRequest

        // 5. Fragmented multi-rect FramebufferUpdate:
        //    Rect 0: Raw 2x2 at (0,0) filled with red (0x00FF0000 in LE XRGB)
        //    Rect 1: Overlapping CopyRect 2x2 from (0,0) to (1,1)
        QByteArray fbu;
        fbu.append(char(0)); // FramebufferUpdate
        fbu.append(char(0)); // padding
        appendU16BE(fbu, 2); // 2 rects
        // Rect 0: Raw
        appendU16BE(fbu, 0); appendU16BE(fbu, 0);
        appendU16BE(fbu, 2); appendU16BE(fbu, 2);
        appendS32BE(fbu, 0); // Raw
        const quint32 redLe = qToLittleEndian<quint32>(0x00FF0000u);
        for (int i = 0; i < 4; ++i) {
            fbu.append(reinterpret_cast<const char *>(&redLe), 4);
        }
        // Rect 1: Overlapping CopyRect (0,0)->(1,1)
        appendU16BE(fbu, 1); appendU16BE(fbu, 1);
        appendU16BE(fbu, 2); appendU16BE(fbu, 2);
        appendS32BE(fbu, 1); // CopyRect
        appendU16BE(fbu, 0); appendU16BE(fbu, 0);

        // Feed 3 bytes at a time to stress fragmentation
        bool sawDirty = false;
        out.clear();
        for (int i = 0; i < fbu.size(); i += 3) {
            QByteArray chunkOut;
            QVERIFY2(s.feed(fbu.mid(i, 3), chunkOut, dirty, err), qPrintable(err));
            out.append(chunkOut);
            sawDirty = sawDirty || dirty;
        }
        QVERIFY(sawDirty);
        // Must have requested next incremental update (10 bytes, incremental=1)
        QCOMPARE(out.size(), 10);
        QCOMPARE(quint8(out[0]), quint8(3));
        QCOMPARE(quint8(out[1]), quint8(1));
        QCOMPARE(qRed(s.framebuffer().pixel(0, 0)), 255);
        QCOMPARE(qRed(s.framebuffer().pixel(2, 2)), 255);
        QCOMPARE(qRed(s.framebuffer().pixel(3, 3)), 0);

        // 6. DesktopSize pseudo-encoding (-223) resizes cleanly to 800x480
        QByteArray dsz;
        dsz.append(char(0)); dsz.append(char(0));
        appendU16BE(dsz, 1);
        appendU16BE(dsz, 0); appendU16BE(dsz, 0);
        appendU16BE(dsz, 800); appendU16BE(dsz, 480);
        appendS32BE(dsz, -223);
        out.clear();
        QVERIFY(s.feed(dsz, out, dirty, err));
        QCOMPARE(s.framebuffer().size(), QSize(800, 480));
    }

    void testRfbPointerCoordinateClamping() {
        RfbScreenWidget w;
        w.resize(800, 480);
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(0, 0)), QPoint(0, 0));
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(799, 479)), QPoint(799, 479));
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(-50, -10)), QPoint(0, 0));
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(1200, 900)), QPoint(799, 479));

        // Scaled widget still maps accurately across 800x480
        w.resize(400, 240);
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(200, 120)), QPoint(400, 240));

        // PointerEvent wire encoding (type=5, mask, BE x, BE y)
        QByteArray ev = RfbSession::buildPointerEvent(1, 320, 240);
        QCOMPARE(ev.size(), 6);
        QCOMPARE(quint8(ev[0]), quint8(5));
        QCOMPARE(quint8(ev[1]), quint8(1));
        QCOMPARE(qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(ev.constData() + 2)), quint16(320));
        QCOMPARE(qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(ev.constData() + 4)), quint16(240));
    }
};

QTEST_MAIN(TestRcd330Gui)
#include "test_rcd330_gui.moc"
```

- [ ] **Step 2: Write `rfb_protocol.{h,cpp}` and `rfb_widget.{h,cpp}` minimal stubs, verify tests fail (or write stubs that return false), then implement full protocol & widget**

Create `tools/rcd330_gui/src/rfb_protocol.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <cstdint>

class RfbSession {
public:
    enum class State {
        AwaitingVersion,
        AwaitingSecurityTypes,
        AwaitingSecurityResult,
        AwaitingServerInit,
        Ready,
        Error,
    };

    RfbSession();
    void reset();
    bool feed(const QByteArray &chunk, QByteArray &outToServer, bool &frameDirty, QString &errorOut);

    static QByteArray buildPointerEvent(uint8_t buttonMask, uint16_t x, uint16_t y);
    static QByteArray buildFramebufferUpdateRequest(bool incremental, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    const QImage &framebuffer() const { return m_fb; }
    State state() const { return m_state; }

private:
    enum class MsgPhase {
        Idle,
        FbuHeader,
        RectHeader,
        RawPixels,
        CopyRectSrc,
        SetColorMapSkip,
        ServerCutTextLen,
        ServerCutTextBody,
    };

    static QByteArray buildSetPixelFormat32Xrgb();
    static QByteArray buildSetEncodings();

    State m_state = State::AwaitingVersion;
    MsgPhase m_phase = MsgPhase::Idle;
    QByteArray m_buf;
    QImage m_fb;
    uint16_t m_rectsRemaining = 0;
    uint16_t m_rx = 0;
    uint16_t m_ry = 0;
    uint16_t m_rw = 0;
    uint16_t m_rh = 0;
    uint32_t m_skipBytes = 0;
};
```

Create `tools/rcd330_gui/src/rfb_protocol.cpp`:

```cpp
#include "rfb_protocol.h"

#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace {
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 480;

void putU16(QByteArray &out, uint16_t v) {
    char b[2];
    qToBigEndian(v, b);
    out.append(b, 2);
}

void putU32(QByteArray &out, uint32_t v) {
    char b[4];
    qToBigEndian(v, b);
    out.append(b, 4);
}

void putS32(QByteArray &out, int32_t v) {
    putU32(out, static_cast<uint32_t>(v));
}

uint16_t getU16(const char *p) {
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

uint32_t getU32(const char *p) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

int32_t getS32(const char *p) {
    return static_cast<int32_t>(getU32(p));
}
} // namespace

RfbSession::RfbSession() {
    reset();
}

void RfbSession::reset() {
    m_state = State::AwaitingVersion;
    m_phase = MsgPhase::Idle;
    m_buf.clear();
    m_fb = QImage(kDefaultWidth, kDefaultHeight, QImage::Format_RGB32);
    m_fb.fill(Qt::black);
    m_rectsRemaining = 0;
    m_rx = m_ry = m_rw = m_rh = 0;
    m_skipBytes = 0;
}

QByteArray RfbSession::buildSetPixelFormat32Xrgb() {
    QByteArray out;
    out.reserve(20);
    out.append(char(0));          // message-type = SetPixelFormat
    out.append(3, char(0));       // padding
    out.append(char(32));         // bits-per-pixel
    out.append(char(24));         // depth
    out.append(char(0));          // big-endian-flag = 0 (little-endian host)
    out.append(char(1));          // true-colour-flag = 1
    putU16(out, 255);             // red-max
    putU16(out, 255);             // green-max
    putU16(out, 255);             // blue-max
    out.append(char(16));         // red-shift
    out.append(char(8));          // green-shift
    out.append(char(0));          // blue-shift
    out.append(3, char(0));       // padding
    return out;
}

QByteArray RfbSession::buildSetEncodings() {
    QByteArray out;
    out.reserve(16);
    out.append(char(2));          // message-type = SetEncodings
    out.append(char(0));          // padding
    putU16(out, 3);               // number-of-encodings
    putS32(out, 0);               // Raw
    putS32(out, 1);               // CopyRect
    putS32(out, -223);            // DesktopSize pseudo-encoding
    return out;
}

QByteArray RfbSession::buildFramebufferUpdateRequest(bool incremental, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    QByteArray out;
    out.reserve(10);
    out.append(char(3));
    out.append(char(incremental ? 1 : 0));
    putU16(out, x);
    putU16(out, y);
    putU16(out, w);
    putU16(out, h);
    return out;
}

QByteArray RfbSession::buildPointerEvent(uint8_t buttonMask, uint16_t x, uint16_t y) {
    QByteArray out;
    out.reserve(6);
    out.append(char(5));
    out.append(char(buttonMask));
    putU16(out, x);
    putU16(out, y);
    return out;
}

bool RfbSession::feed(const QByteArray &chunk, QByteArray &outToServer, bool &frameDirty, QString &errorOut) {
    frameDirty = false;
    if (m_state == State::Error) {
        return false;
    }
    m_buf.append(chunk);

    while (true) {
        if (m_state == State::AwaitingVersion) {
            if (m_buf.size() < 12) return true;
            if (!m_buf.startsWith("RFB ")) {
                m_state = State::Error;
                errorOut = "Invalid RFB server banner";
                return false;
            }
            m_buf.remove(0, 12);
            outToServer.append("RFB 003.008\n");
            m_state = State::AwaitingSecurityTypes;
            continue;
        }

        if (m_state == State::AwaitingSecurityTypes) {
            if (m_buf.isEmpty()) return true;
            const uint8_t count = static_cast<uint8_t>(m_buf[0]);
            if (count == 0) {
                if (m_buf.size() < 5) return true;
                const uint32_t reasonLen = getU32(m_buf.constData() + 1);
                if (static_cast<uint32_t>(m_buf.size()) < 5 + reasonLen) return true;
                m_state = State::Error;
                errorOut = QString::fromUtf8(m_buf.mid(5, static_cast<int>(reasonLen)));
                return false;
            }
            if (m_buf.size() < 1 + count) return true;
            bool hasNone = false;
            for (int i = 0; i < count; ++i) {
                if (static_cast<uint8_t>(m_buf[1 + i]) == 1) {
                    hasNone = true;
                    break;
                }
            }
            m_buf.remove(0, 1 + count);
            if (!hasNone) {
                m_state = State::Error;
                errorOut = "RFB server does not support SecurityType None";
                return false;
            }
            outToServer.append(char(1));
            m_state = State::AwaitingSecurityResult;
            continue;
        }

        if (m_state == State::AwaitingSecurityResult) {
            if (m_buf.size() < 4) return true;
            const uint32_t status = getU32(m_buf.constData());
            m_buf.remove(0, 4);
            if (status != 0) {
                m_state = State::Error;
                errorOut = "RFB security handshake rejected";
                return false;
            }
            outToServer.append(char(1)); // ClientInit shared=1
            m_state = State::AwaitingServerInit;
            continue;
        }

        if (m_state == State::AwaitingServerInit) {
            if (m_buf.size() < 24) return true;
            const uint16_t w = getU16(m_buf.constData() + 0);
            const uint16_t h = getU16(m_buf.constData() + 2);
            const uint32_t nameLen = getU32(m_buf.constData() + 20);
            if (nameLen > 65536 || w == 0 || h == 0 || w > 4096 || h > 4096) {
                m_state = State::Error;
                errorOut = "Invalid RFB ServerInit dimensions";
                return false;
            }
            if (static_cast<uint32_t>(m_buf.size()) < 24 + nameLen) return true;
            m_buf.remove(0, static_cast<int>(24 + nameLen));
            m_fb = QImage(w, h, QImage::Format_RGB32);
            m_fb.fill(Qt::black);
            m_state = State::Ready;
            m_phase = MsgPhase::Idle;
            outToServer.append(buildSetPixelFormat32Xrgb());
            outToServer.append(buildSetEncodings());
            outToServer.append(buildFramebufferUpdateRequest(false, 0, 0, w, h));
            continue;
        }

        // State::Ready
        if (m_phase == MsgPhase::Idle) {
            if (m_buf.isEmpty()) return true;
            const uint8_t msgType = static_cast<uint8_t>(m_buf[0]);
            if (msgType == 0) {
                m_buf.remove(0, 1);
                m_phase = MsgPhase::FbuHeader;
            } else if (msgType == 1) {
                if (m_buf.size() < 6) return true;
                const uint16_t numColors = getU16(m_buf.constData() + 4);
                m_buf.remove(0, 6);
                m_skipBytes = static_cast<uint32_t>(numColors) * 6u;
                m_phase = MsgPhase::SetColorMapSkip;
            } else if (msgType == 2) {
                m_buf.remove(0, 1); // Bell
            } else if (msgType == 3) {
                m_buf.remove(0, 1);
                m_phase = MsgPhase::ServerCutTextLen;
            } else {
                m_state = State::Error;
                errorOut = QString("Unsupported RFB server message type %1").arg(msgType);
                return false;
            }
            continue;
        }

        if (m_phase == MsgPhase::SetColorMapSkip || m_phase == MsgPhase::ServerCutTextBody) {
            if (static_cast<uint32_t>(m_buf.size()) < m_skipBytes) return true;
            m_buf.remove(0, static_cast<int>(m_skipBytes));
            m_skipBytes = 0;
            m_phase = MsgPhase::Idle;
            continue;
        }

        if (m_phase == MsgPhase::ServerCutTextLen) {
            if (m_buf.size() < 7) return true; // 3 pad + 4 len
            m_skipBytes = getU32(m_buf.constData() + 3);
            m_buf.remove(0, 7);
            m_phase = MsgPhase::ServerCutTextBody;
            continue;
        }

        if (m_phase == MsgPhase::FbuHeader) {
            if (m_buf.size() < 3) return true; // 1 pad + 2 numRects
            m_rectsRemaining = getU16(m_buf.constData() + 1);
            m_buf.remove(0, 3);
            if (m_rectsRemaining == 0) {
                m_phase = MsgPhase::Idle;
                outToServer.append(buildFramebufferUpdateRequest(
                    true, 0, 0,
                    static_cast<uint16_t>(m_fb.width()),
                    static_cast<uint16_t>(m_fb.height())));
            } else {
                m_phase = MsgPhase::RectHeader;
            }
            continue;
        }

        if (m_phase == MsgPhase::RectHeader) {
            if (m_buf.size() < 12) return true;
            m_rx = getU16(m_buf.constData() + 0);
            m_ry = getU16(m_buf.constData() + 2);
            m_rw = getU16(m_buf.constData() + 4);
            m_rh = getU16(m_buf.constData() + 6);
            const int32_t enc = getS32(m_buf.constData() + 8);
            m_buf.remove(0, 12);

            if (enc == 0) {
                m_phase = MsgPhase::RawPixels;
            } else if (enc == 1) {
                m_phase = MsgPhase::CopyRectSrc;
            } else if (enc == -223) {
                if (m_rw > 0 && m_rh > 0 && m_rw <= 4096 && m_rh <= 4096) {
                    m_fb = QImage(m_rw, m_rh, QImage::Format_RGB32);
                    m_fb.fill(Qt::black);
                    frameDirty = true;
                }
                --m_rectsRemaining;
                if (m_rectsRemaining == 0) {
                    m_phase = MsgPhase::Idle;
                    outToServer.append(buildFramebufferUpdateRequest(
                        false, 0, 0,
                        static_cast<uint16_t>(m_fb.width()),
                        static_cast<uint16_t>(m_fb.height())));
                }
            } else {
                m_state = State::Error;
                errorOut = QString("Unsupported RFB encoding %1").arg(enc);
                return false;
            }
            continue;
        }

        if (m_phase == MsgPhase::RawPixels) {
            const qsizetype needed = static_cast<qsizetype>(m_rw) * static_cast<qsizetype>(m_rh) * 4;
            if (m_buf.size() < needed) return true;
            const char *src = m_buf.constData();
            const int maxY = std::min<int>(m_fb.height(), m_ry + m_rh);
            const int copyCols = std::max(0, std::min<int>(m_fb.width() - m_rx, m_rw));
            for (int row = 0; row < (maxY - m_ry); ++row) {
                uchar *dst = m_fb.scanLine(m_ry + row) + static_cast<qsizetype>(m_rx) * 4;
                std::memcpy(dst, src + static_cast<qsizetype>(row) * m_rw * 4,
                            static_cast<size_t>(copyCols) * 4);
            }
            m_buf.remove(0, needed);
            frameDirty = true;
            --m_rectsRemaining;
            if (m_rectsRemaining == 0) {
                m_phase = MsgPhase::Idle;
                outToServer.append(buildFramebufferUpdateRequest(
                    true, 0, 0,
                    static_cast<uint16_t>(m_fb.width()),
                    static_cast<uint16_t>(m_fb.height())));
            } else {
                m_phase = MsgPhase::RectHeader;
            }
            continue;
        }

        if (m_phase == MsgPhase::CopyRectSrc) {
            if (m_buf.size() < 4) return true;
            const uint16_t sx = getU16(m_buf.constData() + 0);
            const uint16_t sy = getU16(m_buf.constData() + 2);
            m_buf.remove(0, 4);
            const QImage patch = m_fb.copy(sx, sy, m_rw, m_rh);
            const int maxY = std::min<int>(m_fb.height(), m_ry + patch.height());
            const int copyCols = std::max(0, std::min<int>(m_fb.width() - m_rx, patch.width()));
            for (int row = 0; row < (maxY - m_ry); ++row) {
                std::memcpy(m_fb.scanLine(m_ry + row) + static_cast<qsizetype>(m_rx) * 4,
                            patch.constScanLine(row),
                            static_cast<size_t>(copyCols) * 4);
            }
            frameDirty = true;
            --m_rectsRemaining;
            if (m_rectsRemaining == 0) {
                m_phase = MsgPhase::Idle;
                outToServer.append(buildFramebufferUpdateRequest(
                    true, 0, 0,
                    static_cast<uint16_t>(m_fb.width()),
                    static_cast<uint16_t>(m_fb.height())));
            } else {
                m_phase = MsgPhase::RectHeader;
            }
            continue;
        }
    }
}
```

Create `tools/rcd330_gui/src/rfb_widget.h`:

```cpp
#pragma once

#include "rfb_protocol.h"
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

class RfbScreenWidget : public QWidget {
    Q_OBJECT

public:
    explicit RfbScreenWidget(QWidget *parent = nullptr);
    ~RfbScreenWidget() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }
    QPoint mapWidgetToFramebuffer(const QPointF &pos) const;

signals:
    void connectionChanged(bool connected);

protected:
    QSize sizeHint() const override { return QSize(800, 480); }
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void attemptConnect();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);

private:
    void sendPointer(uint8_t mask, const QPoint &fbPos);
    void releaseTouchIfActive();

    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    RfbSession m_session;
    QString m_host = "127.0.0.1";
    quint16 m_port = 5900;
    bool m_wantConnect = false;
    bool m_connected = false;
    bool m_touchDown = false;
    QPoint m_lastFbPos{-1, -1};
    QString m_statusText = "Connecting to QEMU display...";
};
```

Create `tools/rcd330_gui/src/rfb_widget.cpp`:

```cpp
#include "rfb_widget.h"

#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

RfbScreenWidget::RfbScreenWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(400, 240);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    resize(800, 480);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_reconnectTimer.setInterval(250);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RfbScreenWidget::attemptConnect);

    connect(&m_socket, &QTcpSocket::connected, this, &RfbScreenWidget::onSocketConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &RfbScreenWidget::onSocketDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &RfbScreenWidget::onSocketReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &RfbScreenWidget::onSocketError);
}

RfbScreenWidget::~RfbScreenWidget() {
    disconnectFromServer();
}

void RfbScreenWidget::connectToServer(const QString &host, quint16 port) {
    m_host = host;
    m_port = port;
    m_wantConnect = true;
    m_statusText = QString("Connecting to QEMU display (%1:%2)...").arg(host).arg(port);
    update();
    attemptConnect();
}

void RfbScreenWidget::disconnectFromServer() {
    m_wantConnect = false;
    m_reconnectTimer.stop();
    releaseTouchIfActive();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
}

void RfbScreenWidget::attemptConnect() {
    if (!m_wantConnect) return;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_session.reset();
    m_socket.connectToHost(m_host, m_port);
}

void RfbScreenWidget::onSocketConnected() {
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_session.reset();
    m_statusText = "Negotiating display...";
    update();
}

void RfbScreenWidget::onSocketDisconnected() {
    m_touchDown = false;
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect) {
        m_statusText = "Reconnecting to QEMU display...";
        update();
        m_reconnectTimer.start();
    }
}

void RfbScreenWidget::onSocketError(QAbstractSocket::SocketError) {
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect && !m_reconnectTimer.isActive()) {
        m_statusText = "Waiting for QEMU display...";
        update();
        m_reconnectTimer.start();
    }
}

void RfbScreenWidget::onSocketReadyRead() {
    const QByteArray chunk = m_socket.readAll();
    QByteArray reply;
    bool dirty = false;
    QString err;
    const bool wasReady = (m_session.state() == RfbSession::State::Ready);

    if (!m_session.feed(chunk, reply, dirty, err)) {
        m_statusText = QString("Display protocol error: %1").arg(err);
        m_socket.abort();
        update();
        if (m_wantConnect) m_reconnectTimer.start();
        return;
    }

    if (!reply.isEmpty()) {
        m_socket.write(reply);
    }

    if (!wasReady && m_session.state() == RfbSession::State::Ready) {
        m_connected = true;
        emit connectionChanged(true);
        update();
    } else if (dirty) {
        update();
    }
}

QPoint RfbScreenWidget::mapWidgetToFramebuffer(const QPointF &pos) const {
    const int fbW = std::max(1, m_session.framebuffer().width());
    const int fbH = std::max(1, m_session.framebuffer().height());
    const double w = std::max(1.0, static_cast<double>(width()));
    const double h = std::max(1.0, static_cast<double>(height()));
    const int fx = std::clamp(static_cast<int>(std::lround(pos.x() * fbW / w)), 0, fbW - 1);
    const int fy = std::clamp(static_cast<int>(std::lround(pos.y() * fbH / h)), 0, fbH - 1);
    return QPoint(fx, fy);
}

void RfbScreenWidget::sendPointer(uint8_t mask, const QPoint &fbPos) {
    if (!m_connected || m_socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    m_socket.write(RfbSession::buildPointerEvent(
        mask, static_cast<uint16_t>(fbPos.x()), static_cast<uint16_t>(fbPos.y())));
}

void RfbScreenWidget::releaseTouchIfActive() {
    if (m_touchDown) {
        m_touchDown = false;
        const QPoint p = (m_lastFbPos.x() >= 0) ? m_lastFbPos : QPoint(0, 0);
        sendPointer(0, p);
    }
}

void RfbScreenWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        m_touchDown = true;
        m_lastFbPos = mapWidgetToFramebuffer(event->position());
        sendPointer(1, m_lastFbPos);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void RfbScreenWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_touchDown) {
        const QPoint p = mapWidgetToFramebuffer(event->position());
        if (p != m_lastFbPos) {
            m_lastFbPos = p;
            sendPointer(1, m_lastFbPos);
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void RfbScreenWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_touchDown) {
        m_touchDown = false;
        m_lastFbPos = mapWidgetToFramebuffer(event->position());
        sendPointer(0, m_lastFbPos);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void RfbScreenWidget::focusOutEvent(QFocusEvent *event) {
    releaseTouchIfActive();
    QWidget::focusOutEvent(event);
}

void RfbScreenWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    if (m_connected) {
        p.drawImage(rect(), m_session.framebuffer());
    } else {
        p.fillRect(rect(), QColor(0x0b, 0x0e, 0x13));
        p.setPen(QColor(0x8b, 0x97, 0xa8));
        QFont f = font();
        f.setPointSize(13);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_statusText);
    }
}
```

- [ ] **Step 3: Build & run headless Qt6 unit tests**

Run: `./tools/build_gui.sh --test`
Expected: `rcd330_gui_core` and `test_rcd330_gui` build cleanly and `ctest` PASSES (`testRfbHandshakeFragmentedRawAndCopyRect`, `testRfbPointerCoordinateClamping`).

- [ ] **Step 4: Commit**

```bash
git add tools/build_gui.sh tools/rcd330_gui/CMakeLists.txt \
        tools/rcd330_gui/src/rfb_protocol.h tools/rcd330_gui/src/rfb_protocol.cpp \
        tools/rcd330_gui/src/rfb_widget.h tools/rcd330_gui/src/rfb_widget.cpp \
        tools/rcd330_gui/tests/test_rcd330_gui.cpp
git commit -m "gui: scaffold Qt6 project, build_gui.sh, and RFB 3.8 800x480 touchscreen widget"
```

---

### Task 4: `VipClient`, `CodingBridge`, Long-Press `BezelButton` & `RotaryKnobWidget`

**Files:**
- Create: `tools/rcd330_gui/src/vip_client.h`
- Create: `tools/rcd330_gui/src/vip_client.cpp`
- Create: `tools/rcd330_gui/src/coding_bridge.h`
- Create: `tools/rcd330_gui/src/coding_bridge.cpp`
- Create: `tools/rcd330_gui/src/bezel_button.h`
- Create: `tools/rcd330_gui/src/bezel_button.cpp`
- Create: `tools/rcd330_gui/src/rotary_knob_widget.h`
- Create: `tools/rcd330_gui/src/rotary_knob_widget.cpp`
- Modify: `tools/rcd330_gui/CMakeLists.txt`
- Modify: `tools/rcd330_gui/tests/test_rcd330_gui.cpp`

**Interfaces:**
- Consumes: `tools/rcd330_coding.py` (`load`, `apply`, `reset` CLI subcommands from Task 2), QEMU VIP TCP protocol (`127.0.0.1:4444`).
- Produces:
  - `class VipClient : public QObject`:
    - `void connectToServer(const QString &host, quint16 port);`
    - `void disconnectFromServer();`
    - `bool isConnected() const;`
    - `bool sendCommand(const QString &cmd, std::function<void(const QString &)> callback = {});` (returns `false` on empty or `\r`/`\n` injection)
    - `void sendCommands(const QStringList &cmds, std::function<void(const QStringList &)> callback = {});`
    - `QStringList logLines() const;`
    - `void clearLog();`
    - Signals: `void connectionChanged(bool connected);`, `void logAppended(const QString &line);`, `void rebootObserved();`
  - `struct CodingPayload` & `class CodingBridge`:
    - `bool load(CodingPayload &out, QString &errorOut) const;`
    - `bool apply(const QJsonObject &update, bool live, CodingPayload &out, QString &errorOut) const;`
    - `bool reset(bool live, CodingPayload &out, QString &errorOut) const;`
  - `class BezelButton : public QWidget`:
    - `enum class HoldMode { StandardHardkey, PowerKey, InstantKey };`
    - `BezelButton(const QString &label, const QString &keyName, HoldMode mode = HoldMode::StandardHardkey, QWidget *parent = nullptr);`
    - `void startScriptedHold(int durationMs);`
    - `void cancelHold();`
    - `bool isHeld() const;`
    - Signals: `void keyPressed(const QString &keyName);`, `void keyReleased(const QString &keyName);`
  - `class RotaryKnobWidget : public QWidget`:
    - `RotaryKnobWidget(const QString &title, const QString &decLabel, const QString &centerLabel, const QString &centerKey, BezelButton::HoldMode centerMode, const QString &incLabel, QWidget *parent = nullptr);`
    - `BezelButton *centerButton() const;`
    - Signals: `void stepped(int delta);`

- [ ] **Step 1: Add failing tests for `VipClient`, `CodingBridge`, `BezelButton`, and `RotaryKnobWidget` to `tools/rcd330_gui/tests/test_rcd330_gui.cpp`**

Add these test slots to `TestRcd330Gui` (covering Review Focus #1, #3, #4):

```cpp
    void testVipClientProtocolHardening() {
        QTcpServer srv;
        QVERIFY(srv.listen(QHostAddress::LocalHost, 0));

        VipClient cli;
        // Must reject newline / CR injection before sending
        QVERIFY(!cli.sendCommand("TUNER 9850 TEST\nRAW 1 1 2 3"));
        QVERIFY(!cli.sendCommand("PING\rOK"));
        QVERIFY(!cli.sendCommand("   "));

        QTcpSocket *serverSide = nullptr;
        connect(&srv, &QTcpServer::newConnection, &srv, [&]() {
            serverSide = srv.nextPendingConnection();
        });

        cli.connectToServer("127.0.0.1", srv.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(serverSide != nullptr, 2000);

        // Interleave async HOST_TX and split reply across two TCP chunks
        QString pingReply;
        bool sawReboot = false;
        connect(&cli, &VipClient::rebootObserved, &cli, [&]() { sawReboot = true; });
        QVERIFY(cli.sendCommand("PING", [&](const QString &r) { pingReply = r; }));

        QTRY_VERIFY_WITH_TIMEOUT(serverSide->bytesAvailable() >= 5, 2000);
        QCOMPARE(serverSide->readAll(), QByteArray("PING\n"));

        serverSide->write("HOST_TX 0x05 0x01 len=3\nREBOOT POWER_10S\nPONG rcd");
        serverSide->flush();
        QTest::qWait(30);
        QVERIFY(pingReply.isEmpty());
        QVERIFY(sawReboot);

        serverSide->write("330-vip\n");
        serverSide->flush();
        QTRY_COMPARE_WITH_TIMEOUT(pingReply, QString("PONG rcd330-vip"), 2000);
        QVERIFY(cli.isConnected());
    }

    void testCodingBridgeRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = QDir(QStringLiteral(RCD330_REPO_ROOT)).filePath("tools/rcd330_coding.py");
        const QString codingFile = dir.filePath("coding.json");

        CodingBridge bridge(script, codingFile, "5624_VW");
        CodingPayload p;
        QString err;
        QVERIFY2(bridge.load(p, err), qPrintable(err));
        QCOMPARE(p.platform, QString("PQ"));
        QVERIFY(p.rvcEnabled);
        QVERIFY(p.pqVehicles.contains(19));

        QJsonObject upd;
        upd["rvc_enabled"] = false;
        upd["ops_mode"] = "360";
        upd["eng_mode"] = true;
        QVERIFY2(bridge.apply(upd, true, p, err), qPrintable(err));
        QVERIFY(!p.rvcEnabled);
        QCOMPARE(p.opsMode, QString("360"));
        QVERIFY(p.engMode);
        QVERIFY(QFileInfo::exists(codingFile));

        QVERIFY2(bridge.reset(true, p, err), qPrintable(err));
        QVERIFY(p.rvcEnabled);
        QCOMPARE(p.opsMode, QString("8K"));
        QVERIFY(!p.engMode);
    }

    void testBezelButtonLongPressAndSafetyGuards() {
        BezelButton btn("SETUP", "SETUP", BezelButton::HoldMode::StandardHardkey);
        btn.show();
        QStringList events;
        connect(&btn, &BezelButton::keyPressed, &btn, [&](const QString &k) { events << ("DOWN:" + k); });
        connect(&btn, &BezelButton::keyReleased, &btn, [&](const QString &k) { events << ("UP:" + k); });

        // 1. Manual mouse press/release
        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP"}));
        QTest::mouseRelease(&btn, Qt::LeftButton);
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        // 2. Scripted quick-hold auto-releases after target duration
        events.clear();
        btn.startScriptedHold(80);
        QVERIFY(btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP"}));
        QTRY_VERIFY_WITH_TIMEOUT(!btn.isHeld(), 1000);
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        // 3. Early left-click cancels scripted hold cleanly without duplicate press
        events.clear();
        btn.startScriptedHold(5000);
        QCOMPARE(events, QStringList({"DOWN:SETUP"}));
        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));
        QTest::mouseRelease(&btn, Qt::LeftButton);
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        // 4. Focus loss / hide releases held key (no stuck key)
        events.clear();
        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(btn.isHeld());
        btn.hide();
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));
    }
```

Also pass `target_compile_definitions(test_rcd330_gui PRIVATE RCD330_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")` in `tools/rcd330_gui/CMakeLists.txt`.

- [ ] **Step 2: Create `vip_client.{h,cpp}`, `coding_bridge.{h,cpp}`, `bezel_button.{h,cpp}`, and `rotary_knob_widget.{h,cpp}`**

Create `tools/rcd330_gui/src/vip_client.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

class VipClient : public QObject {
    Q_OBJECT

public:
    explicit VipClient(QObject *parent = nullptr);
    ~VipClient() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }

    bool sendCommand(const QString &cmd, std::function<void(const QString &)> callback = {});
    void sendCommands(const QStringList &cmds, std::function<void(const QStringList &)> callback = {});

    QStringList logLines() const { return m_log; }
    void clearLog();

signals:
    void connectionChanged(bool connected);
    void logAppended(const QString &line);
    void rebootObserved();

private slots:
    void attemptConnect();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError);
    void onReadyRead();
    void onKeepaliveTick();
    void onCmdTimeout();

private:
    struct PendingCmd {
        QString cmd;
        std::function<void(const QString &)> cb;
    };

    void pumpNext();
    void appendLog(const QString &line);
    void failAllPending(const QString &reason);

    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_keepaliveTimer;
    QTimer m_cmdTimeoutTimer;
    QString m_host = "127.0.0.1";
    quint16 m_port = 4444;
    bool m_wantConnect = false;
    bool m_connected = false;
    bool m_inFlight = false;
    PendingCmd m_current;
    QQueue<PendingCmd> m_queue;
    QByteArray m_rxBuf;
    QStringList m_log;
};
```

Create `tools/rcd330_gui/src/vip_client.cpp`:

```cpp
#include "vip_client.h"

#include <memory>

namespace {
constexpr int kMaxLogLines = 250;
}

VipClient::VipClient(QObject *parent) : QObject(parent) {
    m_reconnectTimer.setInterval(400);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &VipClient::attemptConnect);

    m_keepaliveTimer.setInterval(1500);
    connect(&m_keepaliveTimer, &QTimer::timeout, this, &VipClient::onKeepaliveTick);

    m_cmdTimeoutTimer.setInterval(1200);
    m_cmdTimeoutTimer.setSingleShot(true);
    connect(&m_cmdTimeoutTimer, &QTimer::timeout, this, &VipClient::onCmdTimeout);

    connect(&m_socket, &QTcpSocket::connected, this, &VipClient::onConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &VipClient::onDisconnected);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &VipClient::onError);
    connect(&m_socket, &QTcpSocket::readyRead, this, &VipClient::onReadyRead);
}

VipClient::~VipClient() {
    disconnectFromServer();
}

void VipClient::connectToServer(const QString &host, quint16 port) {
    m_host = host;
    m_port = port;
    m_wantConnect = true;
    attemptConnect();
}

void VipClient::disconnectFromServer() {
    m_wantConnect = false;
    m_reconnectTimer.stop();
    m_keepaliveTimer.stop();
    m_cmdTimeoutTimer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    failAllPending("DISCONNECTED");
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
}

void VipClient::attemptConnect() {
    if (!m_wantConnect) return;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_rxBuf.clear();
    m_socket.connectToHost(m_host, m_port);
}

void VipClient::onConnected() {
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_rxBuf.clear();
    m_keepaliveTimer.start();
    // Verify handshake with initial PING if nothing is already queued
    if (m_queue.isEmpty() && !m_inFlight) {
        sendCommand("PING");
    } else {
        pumpNext();
    }
}

void VipClient::onDisconnected() {
    m_keepaliveTimer.stop();
    m_cmdTimeoutTimer.stop();
    failAllPending("DISCONNECTED");
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect) {
        m_reconnectTimer.start();
    }
}

void VipClient::onError(QAbstractSocket::SocketError) {
    m_keepaliveTimer.stop();
    m_cmdTimeoutTimer.stop();
    failAllPending("ERR socket");
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect && !m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

bool VipClient::sendCommand(const QString &cmd, std::function<void(const QString &)> callback) {
    if (cmd.contains('\n') || cmd.contains('\r')) {
        appendLog("ERR invalid command (newline rejected)");
        if (callback) callback("ERR invalid command");
        return false;
    }
    const QString trimmed = cmd.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    m_queue.enqueue(PendingCmd{trimmed, std::move(callback)});
    pumpNext();
    return true;
}

void VipClient::sendCommands(const QStringList &cmds, std::function<void(const QStringList &)> callback) {
    if (cmds.isEmpty()) {
        if (callback) callback({});
        return;
    }
    auto replies = std::make_shared<QStringList>();
    auto remaining = std::make_shared<int>(cmds.size());
    for (const QString &c : cmds) {
        sendCommand(c, [replies, remaining, callback](const QString &r) {
            replies->append(r);
            if (--(*remaining) == 0 && callback) {
                callback(*replies);
            }
        });
    }
}

void VipClient::pumpNext() {
    if (m_inFlight || m_queue.isEmpty()) return;
    if (m_socket.state() != QAbstractSocket::ConnectedState) return;

    m_current = m_queue.dequeue();
    m_inFlight = true;
    const QByteArray wire = m_current.cmd.toUtf8() + '\n';
    m_socket.write(wire);
    m_socket.flush();
    m_cmdTimeoutTimer.start();
}

void VipClient::onReadyRead() {
    m_rxBuf.append(m_socket.readAll());
    while (true) {
        const int nl = m_rxBuf.indexOf('\n');
        if (nl < 0) break;
        const QString line = QString::fromUtf8(m_rxBuf.left(nl)).trimmed();
        m_rxBuf.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        if (line.startsWith("HOST_TX ")) {
            appendLog(line);
            continue;
        }
        if (line.startsWith("REBOOT ")) {
            appendLog(line);
            emit rebootObserved();
            continue;
        }

        if (m_inFlight) {
            m_cmdTimeoutTimer.stop();
            const PendingCmd done = std::move(m_current);
            m_inFlight = false;
            if (!m_connected && (line.startsWith("PONG") || line.startsWith("OK"))) {
                m_connected = true;
                emit connectionChanged(true);
            }
            if (done.cmd != "PING") {
                appendLog(QString("> %1  |  %2").arg(done.cmd, line));
            }
            if (done.cb) {
                done.cb(line);
            }
            pumpNext();
        } else {
            appendLog(line);
        }
    }
}

void VipClient::onKeepaliveTick() {
    if (m_socket.state() != QAbstractSocket::ConnectedState) return;
    if (!m_inFlight && m_queue.isEmpty()) {
        sendCommand("PING");
    }
}

void VipClient::onCmdTimeout() {
    if (!m_inFlight) return;
    const PendingCmd timedOut = std::move(m_current);
    m_inFlight = false;
    if (timedOut.cmd != "PING") {
        appendLog(QString("> %1  |  TIMEOUT").arg(timedOut.cmd));
    }
    if (timedOut.cb) {
        timedOut.cb("TIMEOUT");
    }
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    m_socket.abort();
    if (m_wantConnect) m_reconnectTimer.start();
}

void VipClient::failAllPending(const QString &reason) {
    if (m_inFlight) {
        const PendingCmd cur = std::move(m_current);
        m_inFlight = false;
        if (cur.cb) cur.cb(reason);
    }
    while (!m_queue.isEmpty()) {
        const PendingCmd p = m_queue.dequeue();
        if (p.cb) p.cb(reason);
    }
}

void VipClient::appendLog(const QString &line) {
    m_log.append(line);
    while (m_log.size() > kMaxLogLines) {
        m_log.removeFirst();
    }
    emit logAppended(line);
}

void VipClient::clearLog() {
    m_log.clear();
}
```

Create `tools/rcd330_gui/src/coding_bridge.h`:

```cpp
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct CodingPayload {
    QString platform = "PQ";
    QString brand = "VW";
    QString canSpeed = "HS";
    QString partNumber = "6RD035187B ";
    QString opsMode = "8K";
    int speakers = 4;
    bool engMode = false;
    int vehicleMqb = 0x38;
    QString codingHex;

    bool btEnabled = true;
    bool rvcEnabled = true;
    bool extMicEnabled = false;
    bool extAmpEnabled = false;
    bool mirrorlinkEnabled = true;
    bool carplayEnabled = true;
    bool androidAutoEnabled = true;
    bool carlifeEnabled = false;
    int vehiclePq = 19;
    bool amDisabled = false;
    int bootlogoId = 0x18;
    bool drivingLock = false;
    QStringList validationWarnings;

    QStringList vipCmds;
    QString codingFile;
    QMap<int, QString> pqVehicles;
    QMap<int, QString> mqbVehicles;
    QMap<int, QString> mqbBootlogos;
};

class CodingBridge {
public:
    CodingBridge(QString scriptPath, QString codingFile, QString fwId);

    bool load(CodingPayload &out, QString &errorOut) const;
    bool apply(const QJsonObject &update, bool live, CodingPayload &out, QString &errorOut) const;
    bool reset(bool live, CodingPayload &out, QString &errorOut) const;

private:
    bool runSubcommand(const QStringList &args, const QByteArray &stdinData,
                       CodingPayload &out, QString &errorOut) const;
    static bool parsePayload(const QByteArray &raw, CodingPayload &out, QString &errorOut);

    QString m_scriptPath;
    QString m_codingFile;
    QString m_fwId;
};
```

Create `tools/rcd330_gui/src/coding_bridge.cpp`:

```cpp
#include "coding_bridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

CodingBridge::CodingBridge(QString scriptPath, QString codingFile, QString fwId)
    : m_scriptPath(std::move(scriptPath)),
      m_codingFile(std::move(codingFile)),
      m_fwId(std::move(fwId)) {}

bool CodingBridge::load(CodingPayload &out, QString &errorOut) const {
    return runSubcommand({"load", "--coding-file", m_codingFile, "--fw-id", m_fwId}, {}, out, errorOut);
}

bool CodingBridge::apply(const QJsonObject &update, bool live, CodingPayload &out, QString &errorOut) const {
    QStringList args{"apply", "--coding-file", m_codingFile, "--fw-id", m_fwId};
    if (live) args << "--live";
    return runSubcommand(args, QJsonDocument(update).toJson(QJsonDocument::Compact), out, errorOut);
}

bool CodingBridge::reset(bool live, CodingPayload &out, QString &errorOut) const {
    QStringList args{"reset", "--coding-file", m_codingFile, "--fw-id", m_fwId};
    if (live) args << "--live";
    return runSubcommand(args, {}, out, errorOut);
}

bool CodingBridge::runSubcommand(const QStringList &args, const QByteArray &stdinData,
                                 CodingPayload &out, QString &errorOut) const {
    QProcess proc;
    QStringList fullArgs;
    fullArgs << m_scriptPath << args;
    proc.start("python3", fullArgs);
    if (!proc.waitForStarted(3000)) {
        errorOut = "Failed to launch python3 for rcd330_coding.py";
        return false;
    }
    if (!stdinData.isEmpty()) {
        proc.write(stdinData);
    }
    proc.closeWriteChannel();
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        errorOut = "rcd330_coding.py timed out";
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        errorOut = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (errorOut.isEmpty()) errorOut = QString("rcd330_coding.py exited with code %1").arg(proc.exitCode());
        return false;
    }
    return parsePayload(proc.readAllStandardOutput(), out, errorOut);
}

bool CodingBridge::parsePayload(const QByteArray &raw, CodingPayload &out, QString &errorOut) {
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        errorOut = QString("Invalid JSON from rcd330_coding.py: %1").arg(perr.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject cfg = root["config"].toObject();
    const QJsonObject dec = root["decoded"].toObject();

    out = CodingPayload{};
    out.platform = cfg["platform"].toString("PQ");
    out.brand = cfg["brand"].toString("VW");
    out.canSpeed = cfg["can_speed"].toString("HS");
    out.partNumber = cfg["part_number"].toString("6RD035187B ");
    out.opsMode = cfg["ops_mode"].toString("8K");
    out.speakers = cfg["speakers"].toInt(4);
    out.engMode = cfg["eng_mode"].toBool(false);
    out.vehicleMqb = cfg["vehicle_mqb"].toInt(0x38);
    out.codingHex = cfg["coding_hex"].toString();

    out.btEnabled = dec["bt_enabled"].toBool(true);
    out.rvcEnabled = dec["rvc_enabled"].toBool(true);
    out.extMicEnabled = dec["ext_mic_enabled"].toBool(false);
    out.extAmpEnabled = dec["ext_amp_enabled"].toBool(false);
    out.mirrorlinkEnabled = dec["mirrorlink_enabled"].toBool(true);
    out.carplayEnabled = dec["carplay_enabled"].toBool(true);
    out.androidAutoEnabled = dec["android_auto_enabled"].toBool(true);
    out.carlifeEnabled = dec["carlife_enabled"].toBool(false);
    out.vehiclePq = dec["vehicle_pq"].toInt(19);
    out.amDisabled = dec["am_disabled"].toBool(false);
    out.bootlogoId = dec["bootlogo_id"].toInt(0x18);
    out.drivingLock = dec["driving_lock"].toBool(false);

    for (const QJsonValue &w : dec["validation_warnings"].toArray()) {
        out.validationWarnings.append(w.toString());
    }
    for (const QJsonValue &c : root["vip_cmds"].toArray()) {
        out.vipCmds.append(c.toString());
    }
    out.codingFile = root["coding_file"].toString();

    auto parseMap = [](const QJsonObject &obj, QMap<int, QString> &dst) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bool ok = false;
            const int k = it.key().toInt(&ok);
            if (ok) dst.insert(k, it.value().toString());
        }
    };
    parseMap(root["pq_vehicles"].toObject(), out.pqVehicles);
    parseMap(root["mqb_vehicles"].toObject(), out.mqbVehicles);
    parseMap(root["mqb_bootlogos"].toObject(), out.mqbBootlogos);
    return true;
}
```

Create `tools/rcd330_gui/src/bezel_button.h`:

```cpp
#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

class BezelButton : public QWidget {
    Q_OBJECT

public:
    enum class HoldMode {
        StandardHardkey,
        PowerKey,
        InstantKey,
    };

    BezelButton(const QString &label,
                const QString &keyName,
                HoldMode mode = HoldMode::StandardHardkey,
                QWidget *parent = nullptr);
    ~BezelButton() override;

    void startScriptedHold(int durationMs);
    void cancelHold();
    bool isHeld() const { return m_state != HoldState::Idle; }
    QString keyName() const { return m_keyName; }

signals:
    void keyPressed(const QString &keyName);
    void keyReleased(const QString &keyName);

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class HoldState { Idle, ManualHold, ScriptedHold };

    void finishHold();

    QString m_label;
    QString m_keyName;
    HoldMode m_mode;
    HoldState m_state = HoldState::Idle;
    int m_scriptedTargetMs = 0;
    QElapsedTimer m_elapsed;
    QTimer m_repaintTimer;
    QTimer m_scriptedTimer;
};
```

Create `tools/rcd330_gui/src/bezel_button.cpp`:

```cpp
#include "bezel_button.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

BezelButton::BezelButton(const QString &label, const QString &keyName, HoldMode mode, QWidget *parent)
    : QWidget(parent), m_label(label), m_keyName(keyName), m_mode(mode) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    if (m_mode == HoldMode::PowerKey) {
        setToolTip("Short press: Standby / Wake\nHold 10 s (or right-click): Hard Reboot");
    } else if (m_mode == HoldMode::StandardHardkey) {
        setToolTip("Click & hold for real long press (2s Long / 4s Service / 9s Eng)\nRight-click for quick-hold shortcuts");
    }

    m_repaintTimer.setInterval(50);
    connect(&m_repaintTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));

    m_scriptedTimer.setSingleShot(true);
    connect(&m_scriptedTimer, &QTimer::timeout, this, &BezelButton::finishHold);
}

BezelButton::~BezelButton() {
    cancelHold();
}

QSize BezelButton::sizeHint() const {
    return QSize(112, 46);
}

void BezelButton::startScriptedHold(int durationMs) {
    if (durationMs <= 0) return;
    if (m_state != HoldState::Idle) {
        finishHold();
    }
    m_state = HoldState::ScriptedHold;
    m_scriptedTargetMs = durationMs;
    m_elapsed.start();
    m_repaintTimer.start();
    m_scriptedTimer.start(durationMs);
    emit keyPressed(m_keyName);
    update();
}

void BezelButton::cancelHold() {
    if (m_state != HoldState::Idle) {
        finishHold();
    }
}

void BezelButton::finishHold() {
    if (m_state == HoldState::Idle) return;
    m_state = HoldState::Idle;
    m_scriptedTargetMs = 0;
    m_repaintTimer.stop();
    m_scriptedTimer.stop();
    emit keyReleased(m_keyName);
    update();
}

void BezelButton::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_state == HoldState::ScriptedHold) {
            // Left click cancels active scripted hold cleanly without re-pressing
            finishHold();
            event->accept();
            return;
        }
        if (m_state == HoldState::Idle) {
            setFocus(Qt::MouseFocusReason);
            m_state = HoldState::ManualHold;
            m_elapsed.start();
            m_repaintTimer.start();
            emit keyPressed(m_keyName);
            update();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void BezelButton::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_state == HoldState::ManualHold) {
            finishHold();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void BezelButton::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    if (m_mode == HoldMode::PowerKey) {
        connect(menu.addAction("Short Tap — Standby / Wake (100 ms)"), &QAction::triggered, this, [this]() {
            startScriptedHold(100);
        });
        connect(menu.addAction("Hard Reboot Hold (10.5 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(10500);
        });
    } else if (m_mode == HoldMode::StandardHardkey) {
        connect(menu.addAction("Short Tap (100 ms)"), &QAction::triggered, this, [this]() {
            startScriptedHold(100);
        });
        connect(menu.addAction("Long Press (2.5 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(2500);
        });
        connect(menu.addAction("Service Hold (4.5 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(4500);
        });
        connect(menu.addAction("Ultra-Long Hold (10.0 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(10000);
        });
    } else {
        connect(menu.addAction("Tap (100 ms)"), &QAction::triggered, this, [this]() {
            startScriptedHold(100);
        });
    }
    if (m_state != HoldState::Idle) {
        menu.addSeparator();
        connect(menu.addAction("Cancel Active Hold"), &QAction::triggered, this, &BezelButton::cancelHold);
    }
    menu.exec(event->globalPos());
}

void BezelButton::focusOutEvent(QFocusEvent *event) {
    if (m_state == HoldState::ManualHold) {
        finishHold();
    }
    QWidget::focusOutEvent(event);
}

void BezelButton::hideEvent(QHideEvent *event) {
    cancelHold();
    QWidget::hideEvent(event);
}

void BezelButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    const bool active = (m_state != HoldState::Idle);
    const qint64 ms = active ? m_elapsed.elapsed() : 0;

    QColor bg(0x1f, 0x26, 0x33);
    QColor border(0x32, 0x3c, 0x4e);
    QColor accent(0x2a, 0x7f, 0xff);

    if (active) {
        bg = QColor(0x27, 0x30, 0x42);
        if (m_mode == HoldMode::PowerKey) {
            accent = (ms >= 10000) ? QColor(0xd9, 0x30, 0x25) : QColor(0xf0, 0x8c, 0x2e);
        } else if (m_mode == HoldMode::StandardHardkey) {
            if (ms >= 9000) accent = QColor(0xd9, 0x30, 0x25);      // Ultra-long / Eng
            else if (ms >= 4000) accent = QColor(0xe6, 0x5c, 0x2e); // Service / Ext-long
            else if (ms >= 2000) accent = QColor(0xf0, 0xb4, 0x29); // Long
        }
        border = accent;
    }

    p.setPen(QPen(border, active ? 1.8 : 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(r, 7.0, 7.0);

    // Bottom progress fill bar while held
    if (active && m_mode != HoldMode::InstantKey) {
        const double maxMs = (m_state == HoldState::ScriptedHold && m_scriptedTargetMs > 0)
                                 ? static_cast<double>(m_scriptedTargetMs)
                                 : (m_mode == HoldMode::PowerKey ? 10000.0 : 9000.0);
        const double frac = std::clamp(static_cast<double>(ms) / maxMs, 0.0, 1.0);
        QRectF bar(r.left() + 4.0, r.bottom() - 5.0, (r.width() - 8.0) * frac, 3.5);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(bar, 1.5, 1.5);
    }

    QFont f = font();
    f.setBold(true);
    f.setPointSize(11);
    p.setFont(f);
    p.setPen(QColor(0xe6, 0xed, 0xf3));

    if (active && m_mode != HoldMode::InstantKey && ms >= 200) {
        const double sec = static_cast<double>(ms) / 1000.0;
        QString tag;
        if (m_mode == HoldMode::PowerKey) {
            tag = (ms >= 10000) ? "REBOOT" : QString("%1s").arg(sec, 0, 'f', 1);
        } else {
            if (ms >= 9000) tag = QString("%1s ENG").arg(sec, 0, 'f', 1);
            else if (ms >= 4000) tag = QString("%1s SVC").arg(sec, 0, 'f', 1);
            else if (ms >= 2000) tag = QString("%1s LNG").arg(sec, 0, 'f', 1);
            else tag = QString("%1s").arg(sec, 0, 'f', 1);
        }
        p.drawText(r.adjusted(4, 2, -4, -14), Qt::AlignCenter, m_label);
        f.setPointSize(9);
        p.setFont(f);
        p.setPen(accent.lighter(120));
        p.drawText(r.adjusted(4, 18, -4, -6), Qt::AlignCenter, tag);
    } else {
        p.drawText(r, Qt::AlignCenter, m_label);
    }
}
```

Create `tools/rcd330_gui/src/rotary_knob_widget.h` and `rotary_knob_widget.cpp`:

```cpp
// rotary_knob_widget.h
#pragma once

#include "bezel_button.h"
#include <QWidget>

class RotaryKnobWidget : public QWidget {
    Q_OBJECT

public:
    RotaryKnobWidget(const QString &title,
                     const QString &decLabel,
                     const QString &centerLabel,
                     const QString &centerKey,
                     BezelButton::HoldMode centerMode,
                     const QString &incLabel,
                     QWidget *parent = nullptr);

    BezelButton *centerButton() const { return m_centerBtn; }

signals:
    void stepped(int delta);

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    BezelButton *m_centerBtn = nullptr;
};
```

```cpp
// rotary_knob_widget.cpp
#include "rotary_knob_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

RotaryKnobWidget::RotaryKnobWidget(const QString &title,
                                   const QString &decLabel,
                                   const QString &centerLabel,
                                   const QString &centerKey,
                                   BezelButton::HoldMode centerMode,
                                   const QString &incLabel,
                                   QWidget *parent)
    : QWidget(parent) {
    setObjectName("knobCard");
    setToolTip("Scroll mouse wheel to turn knob; click/hold center button");

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(6);

    auto *lbl = new QLabel(title, this);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setObjectName("knobTitle");
    vbox->addWidget(lbl);

    auto *row = new QHBoxLayout();
    row->setSpacing(4);

    auto *decBtn = new QPushButton(decLabel, this);
    decBtn->setFixedSize(30, 38);
    decBtn->setAutoRepeat(true);
    decBtn->setAutoRepeatDelay(300);
    decBtn->setAutoRepeatInterval(100);
    connect(decBtn, &QPushButton::clicked, this, [this]() { emit stepped(-1); });

    m_centerBtn = new BezelButton(centerLabel, centerKey, centerMode, this);
    m_centerBtn->setMinimumWidth(48);
    m_centerBtn->setFixedHeight(38);

    auto *incBtn = new QPushButton(incLabel, this);
    incBtn->setFixedSize(30, 38);
    incBtn->setAutoRepeat(true);
    incBtn->setAutoRepeatDelay(300);
    incBtn->setAutoRepeatInterval(100);
    connect(incBtn, &QPushButton::clicked, this, [this]() { emit stepped(1); });

    row->addWidget(decBtn);
    row->addWidget(m_centerBtn, 1);
    row->addWidget(incBtn);
    vbox->addLayout(row);
}

void RotaryKnobWidget::wheelEvent(QWheelEvent *event) {
    const int dy = event->angleDelta().y();
    if (dy != 0) {
        emit stepped(dy > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}
```

Update `tools/rcd330_gui/CMakeLists.txt` to add the new sources to `rcd330_gui_core` and define `RCD330_REPO_ROOT` on `test_rcd330_gui`.

- [ ] **Step 3: Build & run headless Qt6 unit tests**

Run: `./tools/build_gui.sh --test`
Expected: All 5 Qt tests PASS (`testRfbHandshakeFragmentedRawAndCopyRect`, `testRfbPointerCoordinateClamping`, `testVipClientProtocolHardening`, `testCodingBridgeRoundTrip`, `testBezelButtonLongPressAndSafetyGuards`).

- [ ] **Step 4: Commit**

```bash
git add tools/rcd330_gui/CMakeLists.txt \
        tools/rcd330_gui/src/vip_client.h tools/rcd330_gui/src/vip_client.cpp \
        tools/rcd330_gui/src/coding_bridge.h tools/rcd330_gui/src/coding_bridge.cpp \
        tools/rcd330_gui/src/bezel_button.h tools/rcd330_gui/src/bezel_button.cpp \
        tools/rcd330_gui/src/rotary_knob_widget.h tools/rcd330_gui/src/rotary_knob_widget.cpp \
        tools/rcd330_gui/tests/test_rcd330_gui.cpp
git commit -m "gui: add hardened VipClient, CodingBridge, hold-aware BezelButton, and RotaryKnobWidget"
```

---

### Task 5: Unified RCD 330 `MainWindow`, Collapsible Dock Tabs (`VehicleTab`, `CodingTab`, `LogTab`) & `main.cpp`

**Files:**
- Create: `tools/rcd330_gui/src/vehicle_tab.h`
- Create: `tools/rcd330_gui/src/vehicle_tab.cpp`
- Create: `tools/rcd330_gui/src/coding_tab.h`
- Create: `tools/rcd330_gui/src/coding_tab.cpp`
- Create: `tools/rcd330_gui/src/log_tab.h`
- Create: `tools/rcd330_gui/src/log_tab.cpp`
- Create: `tools/rcd330_gui/src/main_window.h`
- Create: `tools/rcd330_gui/src/main_window.cpp`
- Create: `tools/rcd330_gui/src/main.cpp`
- Modify: `tools/rcd330_gui/CMakeLists.txt`
- Modify: `tools/rcd330_gui/tests/test_rcd330_gui.cpp`

**Interfaces:**
- Consumes: `RfbScreenWidget`, `VipClient`, `CodingBridge`, `BezelButton`, `RotaryKnobWidget` from Tasks 3–4.
- Produces:
  - `VehicleTab` emitting `commandRequested(QString)` and `buttonTapRequested(QString)` (with Ignition OFF/CL_S/CL_15, Gear/RVC, Speed, RPM, Fuel, Exterior Temp, Doors, OPS Parking, Sync Host Time, MFSW SWC, FM/RDS presets).
  - `CodingTab` wrapping `CodingBridge` + `VipClient` (full PQ 7B & MQB 25B controls, automatic PQ↔MQB visibility, raw hex editor, validation warnings, `Save & Apply Live`, `Apply Raw Hex`, `Reset Defaults`, plus `syncInitialCodingToVip()`).
  - `LogTab` wrapping `VipClient` log display + Clear + manual VIP command line.
  - `MainWindow` assembling the unified faceplate + collapsible tabbed dock + startup coding sync + QEMU PID liveness monitor.
  - Executable `build/gui/rcd330_gui` accepting `--vip-host`, `--vip-port`, `--vnc-host`, `--vnc-port`, `--fw-name`, `--coding-file`, `--coding-script`, `--qemu-pid`.

- [ ] **Step 1: Implement `VehicleTab`, `CodingTab`, `LogTab`, `MainWindow`, and `main.cpp`**

Create `tools/rcd330_gui/src/vehicle_tab.h` and `vehicle_tab.cpp`:

```cpp
// vehicle_tab.h
#pragma once

#include <QPushButton>
#include <QWidget>

class VehicleTab : public QWidget {
    Q_OBJECT

public:
    explicit VehicleTab(QWidget *parent = nullptr);

signals:
    void commandRequested(const QString &cmd);
    void buttonTapRequested(const QString &keyName);

private:
    void setIgnitionVisual(int level);
    QPushButton *m_ignBtns[3] = {nullptr, nullptr, nullptr};
};
```

```cpp
// vehicle_tab.cpp
#include "vehicle_tab.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

VehicleTab::VehicleTab(QWidget *parent) : QWidget(parent) {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(12);

    // Left column: Emulated Car Components (CAN / BAP)
    auto *canBox = new QGroupBox("Emulated Car Components (CAN / BAP)", this);
    auto *canGrid = new QGridLayout(canBox);
    canGrid->setVerticalSpacing(6);
    canGrid->setHorizontalSpacing(10);
    int row = 0;

    // Ignition Clamp
    canGrid->addWidget(new QLabel("Ignition Clamp", canBox), row, 0);
    auto *ignRow = new QHBoxLayout();
    const char *ignLabels[3] = {"OFF", "CL_S (ACC)", "CL_15 (IGN)"};
    for (int i = 0; i < 3; ++i) {
        m_ignBtns[i] = new QPushButton(ignLabels[i], canBox);
        m_ignBtns[i]->setCheckable(true);
        connect(m_ignBtns[i], &QPushButton::clicked, this, [this, i]() {
            setIgnitionVisual(i);
            emit commandRequested(QString("IGN %1").arg(i));
        });
        ignRow->addWidget(m_ignBtns[i]);
    }
    setIgnitionVisual(2);
    canGrid->addLayout(ignRow, row++, 1, 1, 2);

    // Gear / RVC
    canGrid->addWidget(new QLabel("Gear / RVC", canBox), row, 0);
    auto *revRow = new QHBoxLayout();
    auto *drvBtn = new QPushButton("Drive / Park", canBox);
    auto *revBtn = new QPushButton("Reverse (RVC)", canBox);
    revBtn->setObjectName("warnBtn");
    connect(drvBtn, &QPushButton::clicked, this, [this]() { emit commandRequested("REV 0"); });
    connect(revBtn, &QPushButton::clicked, this, [this]() { emit commandRequested("REV 1"); });
    revRow->addWidget(drvBtn);
    revRow->addWidget(revBtn);
    canGrid->addLayout(revRow, row++, 1, 1, 2);

    // Sliders helper
    auto addSlider = [&](const QString &label, int minV, int maxV, int initV, int step,
                         const QString &cmdPrefix, const QString &suffix) {
        canGrid->addWidget(new QLabel(label, canBox), row, 0);
        auto *sl = new QSlider(Qt::Horizontal, canBox);
        sl->setRange(minV, maxV);
        sl->setSingleStep(step);
        sl->setPageStep(step);
        sl->setValue(initV);
        auto *valLbl = new QLabel(QString("%1%2").arg(initV).arg(suffix), canBox);
        valLbl->setMinimumWidth(68);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(sl, &QSlider::valueChanged, this, [this, valLbl, cmdPrefix, suffix, step](int v) {
            const int snapped = (step > 1) ? ((v / step) * step) : v;
            valLbl->setText(QString("%1%2").arg(snapped).arg(suffix));
            emit commandRequested(QString("%1 %2").arg(cmdPrefix).arg(snapped));
        });
        canGrid->addWidget(sl, row, 1);
        canGrid->addWidget(valLbl, row, 2);
        ++row;
    };

    addSlider("Vehicle Speed", 0, 240, 0, 1, "SPEED", " km/h");
    addSlider("Engine RPM", 0, 7000, 800, 100, "RPM", "");
    addSlider("Fuel Level", 0, 100, 75, 1, "FUEL", "%");
    addSlider("Exterior Temp", -30, 50, 21, 1, "TEMP", " °C");

    // Doors Open
    canGrid->addWidget(new QLabel("Doors Open", canBox), row, 0);
    auto *doorRow = new QHBoxLayout();
    const struct { const char *lbl; const char *cmd; } doors[] = {
        {"All Closed", "DOORS 0"}, {"FL", "DOORS 1"}, {"FR", "DOORS 2"}, {"All Open", "DOORS 15"}
    };
    for (const auto &d : doors) {
        auto *b = new QPushButton(d.lbl, canBox);
        connect(b, &QPushButton::clicked, this, [this, c = QString(d.cmd)]() { emit commandRequested(c); });
        doorRow->addWidget(b);
    }
    canGrid->addLayout(doorRow, row++, 1, 1, 2);

    // OPS Parking
    canGrid->addWidget(new QLabel("OPS Parking", canBox), row, 0);
    auto *opsRow = new QHBoxLayout();
    const struct { const char *lbl; const char *cmd; } ops[] = {
        {"Clear / Close", "OPS 255 255 255 255"},
        {"Close", "OPS 40 30 30 40"},
        {"Critical", "OPS 10 10 10 10"},
    };
    for (const auto &o : ops) {
        auto *b = new QPushButton(o.lbl, canBox);
        connect(b, &QPushButton::clicked, this, [this, c = QString(o.cmd)]() { emit commandRequested(c); });
        opsRow->addWidget(b);
    }
    canGrid->addLayout(opsRow, row++, 1, 1, 2);

    // Cluster Clock
    canGrid->addWidget(new QLabel("Cluster Clock", canBox), row, 0);
    auto *timeBtn = new QPushButton("Sync Host Time to Headunit", canBox);
    connect(timeBtn, &QPushButton::clicked, this, [this]() {
        const QDateTime now = QDateTime::currentDateTime();
        emit commandRequested(QString("TIME %1 %2 %3 %4 %5 %6")
                                  .arg(now.date().year())
                                  .arg(now.date().month())
                                  .arg(now.date().day())
                                  .arg(now.time().hour())
                                  .arg(now.time().minute())
                                  .arg(now.time().second()));
    });
    canGrid->addWidget(timeBtn, row++, 1, 1, 2);

    root->addWidget(canBox, 3);

    // Right column: SWC + FM/RDS Radio Simulator
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(10);

    auto *swcBox = new QGroupBox("Steering Wheel Controls (MFSW CAN)", this);
    auto *swcGrid = new QGridLayout(swcBox);
    const struct { const char *lbl; const char *cmd; bool isKeyTap; } swc[] = {
        {"VOL-", "SWC VOL-", false},
        {"VOL+", "SWC VOL+", false},
        {"◀ PREV", "ENC TUNE -1", false},
        {"NEXT ▶", "ENC TUNE 1", false},
        {"VOICE", "VOICE", true},
        {"MUTE", "SWC MUTE", false},
    };
    for (int i = 0; i < 6; ++i) {
        auto *b = new QPushButton(swc[i].lbl, swcBox);
        if (swc[i].isKeyTap) {
            connect(b, &QPushButton::clicked, this, [this, k = QString(swc[i].cmd)]() { emit buttonTapRequested(k); });
        } else {
            connect(b, &QPushButton::clicked, this, [this, c = QString(swc[i].cmd)]() { emit commandRequested(c); });
        }
        swcGrid->addWidget(b, i / 3, i % 3);
    }
    rightCol->addWidget(swcBox);

    auto *tunerBox = new QGroupBox("FM / RDS Radio Station Simulator", this);
    auto *tunerGrid = new QGridLayout(tunerBox);
    const struct { const char *lbl; const char *cmd; } stations[] = {
        {"87.5 VW RADIO", "TUNER 8750 VW RADIO"},
        {"91.2 BAYERN 3", "TUNER 9120 BAYERN 3"},
        {"95.8 RCD 330", "TUNER 9580 RCD 330"},
        {"98.5 SWR3", "TUNER 9850 SWR3"},
        {"101.4 RADIO 1", "TUNER 10140 RADIO 1"},
        {"104.6 CLASSIC", "TUNER 10460 CLASSIC"},
    };
    for (int i = 0; i < 6; ++i) {
        auto *b = new QPushButton(stations[i].lbl, tunerBox);
        connect(b, &QPushButton::clicked, this, [this, c = QString(stations[i].cmd)]() { emit commandRequested(c); });
        tunerGrid->addWidget(b, i / 2, i % 2);
    }
    rightCol->addWidget(tunerBox);
    rightCol->addStretch(1);

    root->addLayout(rightCol, 2);
}

void VehicleTab::setIgnitionVisual(int level) {
    for (int i = 0; i < 3; ++i) {
        if (m_ignBtns[i]) m_ignBtns[i]->setChecked(i == level);
    }
}
```

Create `tools/rcd330_gui/src/coding_tab.h` and `coding_tab.cpp`:

```cpp
// coding_tab.h
#pragma once

#include "coding_bridge.h"
#include "vip_client.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

class CodingTab : public QWidget {
    Q_OBJECT

public:
    CodingTab(const CodingBridge &bridge, VipClient *vip, QWidget *parent = nullptr);

    void reloadFromDisk();
    void syncLoadedCodingToVip();

    QString currentPlatform() const;
    bool isPqVehicleRowVisible() const;
    bool isMqbVehicleRowVisible() const;

private slots:
    void onPlatformChanged();
    void onSaveStructured();
    void onApplyRawHex();
    void onResetDefaults();

private:
    void renderPayload(const CodingPayload &p);
    void saveWithMode(bool rawHexOnly);
    void updatePlatformVisibility(const QString &plat);

    CodingBridge m_bridge;
    VipClient *m_vip = nullptr;
    CodingPayload m_current;

    QComboBox *m_platform = nullptr;
    QComboBox *m_brand = nullptr;
    QComboBox *m_canSpeed = nullptr;
    QLineEdit *m_partNum = nullptr;
    QComboBox *m_ops = nullptr;
    QComboBox *m_speakers = nullptr;
    QComboBox *m_vehPq = nullptr;
    QComboBox *m_vehMqb = nullptr;
    QComboBox *m_bootlogo = nullptr;

    QWidget *m_rowVehPq = nullptr;
    QWidget *m_rowVehMqb = nullptr;
    QWidget *m_rowBootlogo = nullptr;

    QCheckBox *m_rvc = nullptr;
    QCheckBox *m_bt = nullptr;
    QCheckBox *m_eng = nullptr;
    QCheckBox *m_extAmp = nullptr;
    QCheckBox *m_extMic = nullptr;
    QCheckBox *m_carplay = nullptr;
    QCheckBox *m_aa = nullptr;
    QCheckBox *m_mlink = nullptr;
    QCheckBox *m_carlife = nullptr;
    QCheckBox *m_amDis = nullptr;
    QCheckBox *m_dLock = nullptr;

    QLineEdit *m_hexEdit = nullptr;
    QLabel *m_warnLabel = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_msgLabel = nullptr;
};
```

```cpp
// coding_tab.cpp
#include "coding_tab.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QPushButton>
#include <QVBoxLayout>

CodingTab::CodingTab(const CodingBridge &bridge, VipClient *vip, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_vip(vip) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 8);
    outer->setSpacing(8);

    auto *cols = new QHBoxLayout();
    cols->setSpacing(16);

    // Left column: Platform / Identity / Vehicle / OPS
    auto *leftBox = new QVBoxLayout();
    leftBox->setSpacing(6);

    auto makeRow = [&](const QString &lbl, QWidget *field, QWidget **rowContainerOut = nullptr) {
        auto *w = new QWidget(this);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto *l = new QLabel(lbl, w);
        l->setMinimumWidth(130);
        h->addWidget(l);
        h->addWidget(field, 1);
        leftBox->addWidget(w);
        if (rowContainerOut) *rowContainerOut = w;
    };

    auto *platWrap = new QWidget(this);
    auto *platRow = new QHBoxLayout(platWrap);
    platRow->setContentsMargins(0, 0, 0, 0);
    platRow->setSpacing(6);
    m_platform = new QComboBox(platWrap);
    m_platform->addItem("PQ (7-byte)", "PQ");
    m_platform->addItem("MQB (25-byte)", "MQB");
    m_brand = new QComboBox(platWrap);
    m_brand->addItem("VW", "VW");
    m_brand->addItem("Skoda", "SKODA");
    m_canSpeed = new QComboBox(platWrap);
    m_canSpeed->addItem("CAN HS (500k)", "HS");
    m_canSpeed->addItem("CAN LS (100k)", "LS");
    platRow->addWidget(m_platform);
    platRow->addWidget(m_brand);
    platRow->addWidget(m_canSpeed);
    makeRow("Platform / Brand", platWrap);

    m_partNum = new QLineEdit("6RD035187B ", this);
    m_partNum->setMaxLength(11);
    makeRow("Part Number (11B)", m_partNum);

    auto *opsWrap = new QWidget(this);
    auto *opsRow = new QHBoxLayout(opsWrap);
    opsRow->setContentsMargins(0, 0, 0, 0);
    opsRow->setSpacing(6);
    m_ops = new QComboBox(opsWrap);
    m_ops->addItem("OPS Off", "OFF");
    m_ops->addItem("OPS 4K (Rear)", "4K");
    m_ops->addItem("OPS 8K (F+R)", "8K");
    m_ops->addItem("OPS 360°", "360");
    m_speakers = new QComboBox(opsWrap);
    m_speakers->addItem("4 Speakers (Fader+Bal)", 4);
    m_speakers->addItem("2 Speakers (Bal only)", 2);
    opsRow->addWidget(m_ops);
    opsRow->addWidget(m_speakers);
    makeRow("OPS / Speakers", opsWrap);

    m_vehPq = new QComboBox(this);
    makeRow("PQ Vehicle (B6)", m_vehPq, &m_rowVehPq);
    m_vehMqb = new QComboBox(this);
    makeRow("MQB Vehicle (VRM)", m_vehMqb, &m_rowVehMqb);
    m_bootlogo = new QComboBox(this);
    makeRow("MQB Bootlogo (B18)", m_bootlogo, &m_rowBootlogo);
    leftBox->addStretch(1);

    cols->addLayout(leftBox, 1);

    // Right column: Feature checkboxes + Raw Hex + Actions
    auto *rightBox = new QVBoxLayout();
    rightBox->setSpacing(6);

    auto *chkGrid = new QGridLayout();
    chkGrid->setHorizontalSpacing(12);
    chkGrid->setVerticalSpacing(4);
    m_rvc = new QCheckBox("Rear View Camera (RVC)", this);
    m_bt = new QCheckBox("Bluetooth Enabled", this);
    m_eng = new QCheckBox("Dev / Eng Menu (Ch 50)", this);
    m_extAmp = new QCheckBox("External Amp (B1=02)", this);
    m_extMic = new QCheckBox("External Mic R38 (B3.1)", this);
    m_carplay = new QCheckBox("CarPlay (B0.2)", this);
    m_aa = new QCheckBox("Android Auto (B0.3)", this);
    m_mlink = new QCheckBox("MirrorLink (B0.1)", this);
    m_carlife = new QCheckBox("CarLife (B0.4)", this);
    m_amDis = new QCheckBox("Disable AM Band (B14.1)", this);
    m_dLock = new QCheckBox("Driving Lockout (B23.0)", this);

    QCheckBox *allChecks[] = {
        m_rvc, m_bt, m_eng, m_extAmp, m_extMic, m_carplay,
        m_aa, m_mlink, m_carlife, m_amDis, m_dLock
    };
    for (int i = 0; i < 11; ++i) {
        chkGrid->addWidget(allChecks[i], i / 2, i % 2);
    }
    rightBox->addLayout(chkGrid);

    auto *hexRow = new QHBoxLayout();
    auto *hexLbl = new QLabel("Long Coding Hex", this);
    hexLbl->setMinimumWidth(115);
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setObjectName("hexInput");
    m_hexEdit->setPlaceholderText("0E000440040013");
    hexRow->addWidget(hexLbl);
    hexRow->addWidget(m_hexEdit, 1);
    rightBox->addLayout(hexRow);

    auto *btnRow = new QHBoxLayout();
    auto *saveBtn = new QPushButton("Save & Apply Live", this);
    saveBtn->setObjectName("primaryBtn");
    auto *rawBtn = new QPushButton("Apply Raw Hex", this);
    auto *resetBtn = new QPushButton("Reset Defaults", this);
    connect(saveBtn, &QPushButton::clicked, this, &CodingTab::onSaveStructured);
    connect(rawBtn, &QPushButton::clicked, this, &CodingTab::onApplyRawHex);
    connect(resetBtn, &QPushButton::clicked, this, &CodingTab::onResetDefaults);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(rawBtn);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch(1);
    rightBox->addLayout(btnRow);

    m_warnLabel = new QLabel(this);
    m_warnLabel->setObjectName("warnText");
    m_warnLabel->setWordWrap(true);
    rightBox->addWidget(m_warnLabel);
    rightBox->addStretch(1);

    cols->addLayout(rightBox, 1);
    outer->addLayout(cols);

    auto *metaRow = new QHBoxLayout();
    m_fileLabel = new QLabel("Storage: loading...", this);
    m_fileLabel->setObjectName("mutedText");
    m_msgLabel = new QLabel(this);
    m_msgLabel->setObjectName("mutedText");
    metaRow->addWidget(m_fileLabel, 1);
    metaRow->addWidget(m_msgLabel);
    outer->addLayout(metaRow);

    connect(m_platform, &QComboBox::currentIndexChanged, this, &CodingTab::onPlatformChanged);
    reloadFromDisk();
}

void CodingTab::reloadFromDisk() {
    CodingPayload p;
    QString err;
    if (!m_bridge.load(p, err)) {
        m_warnLabel->setText("Failed to load coding: " + err);
        return;
    }
    renderPayload(p);
}

void CodingTab::syncLoadedCodingToVip() {
    if (!m_vip || m_current.vipCmds.isEmpty()) return;
    CodingPayload p;
    QString err;
    if (m_bridge.load(p, err)) {
        m_current = p;
        m_vip->sendCommands(p.vipCmds);
    }
}

void CodingTab::renderPayload(const CodingPayload &p) {
    m_current = p;

    auto populate = [](QComboBox *box, const QMap<int, QString> &items, bool hexKeys) {
        box->blockSignals(true);
        box->clear();
        for (auto it = items.begin(); it != items.end(); ++it) {
            const QString prefix = hexKeys
                                       ? QString("0x%1").arg(it.key(), 2, 16, QChar('0')).toUpper()
                                       : QString::number(it.key());
            box->addItem(QString("%1 — %2").arg(prefix, it.value()), it.key());
        }
        box->blockSignals(false);
    };

    populate(m_vehPq, p.pqVehicles, false);
    populate(m_vehMqb, p.mqbVehicles, true);
    populate(m_bootlogo, p.mqbBootlogos, true);

    auto selectByData = [](QComboBox *box, const QVariant &val) {
        const int idx = box->findData(val);
        if (idx >= 0) box->setCurrentIndex(idx);
    };

    m_platform->blockSignals(true);
    selectByData(m_platform, p.platform);
    m_platform->blockSignals(false);
    selectByData(m_brand, p.brand);
    selectByData(m_canSpeed, p.canSpeed);
    m_partNum->setText(p.partNumber);
    selectByData(m_ops, p.opsMode);
    selectByData(m_speakers, p.speakers);
    selectByData(m_vehPq, p.vehiclePq);
    selectByData(m_vehMqb, p.vehicleMqb);
    selectByData(m_bootlogo, p.bootlogoId);

    m_rvc->setChecked(p.rvcEnabled);
    m_bt->setChecked(p.btEnabled);
    m_eng->setChecked(p.engMode);
    m_extAmp->setChecked(p.extAmpEnabled);
    m_extMic->setChecked(p.extMicEnabled);
    m_carplay->setChecked(p.carplayEnabled);
    m_aa->setChecked(p.androidAutoEnabled);
    m_mlink->setChecked(p.mirrorlinkEnabled);
    m_carlife->setChecked(p.carlifeEnabled);
    m_amDis->setChecked(p.amDisabled);
    m_dLock->setChecked(p.drivingLock);

    m_hexEdit->setText(p.codingHex);
    m_warnLabel->setText(p.validationWarnings.join('\n'));
    m_fileLabel->setText("Persisted in: " + p.codingFile + " (pre-seeded into /pss1 & /pss2 on next boot)");
    updatePlatformVisibility(p.platform);
}

void CodingTab::updatePlatformVisibility(const QString &plat) {
    const bool isPq = (plat == "PQ");
    m_rowVehPq->setVisible(isPq);
    m_rowVehMqb->setVisible(!isPq);
    m_rowBootlogo->setVisible(!isPq);
    m_extAmp->setVisible(isPq);
    m_extMic->setVisible(isPq);
    m_carplay->setVisible(isPq);
    m_aa->setVisible(isPq);
    m_mlink->setVisible(isPq);
    m_carlife->setVisible(isPq);
    m_amDis->setVisible(!isPq);
    m_dLock->setVisible(!isPq);
}

void CodingTab::onPlatformChanged() {
    updatePlatformVisibility(currentPlatform());
}

QString CodingTab::currentPlatform() const {
    return m_platform->currentData().toString();
}

bool CodingTab::isPqVehicleRowVisible() const {
    return !m_rowVehPq->isHidden();
}

bool CodingTab::isMqbVehicleRowVisible() const {
    return !m_rowVehMqb->isHidden();
}

void CodingTab::onSaveStructured() { saveWithMode(false); }
void CodingTab::onApplyRawHex() { saveWithMode(true); }

void CodingTab::saveWithMode(bool rawHexOnly) {
    const QString plat = currentPlatform();
    QJsonObject body;
    body["platform"] = plat;
    body["brand"] = m_brand->currentData().toString();
    body["can_speed"] = m_canSpeed->currentData().toString();
    body["part_number"] = m_partNum->text();
    body["ops_mode"] = m_ops->currentData().toString();
    body["speakers"] = m_speakers->currentData().toInt();
    body["eng_mode"] = m_eng->isChecked();
    body["vehicle_mqb"] = m_vehMqb->currentData().toInt();

    if (rawHexOnly) {
        body["coding_hex"] = m_hexEdit->text().trimmed();
    } else {
        body["rvc_enabled"] = m_rvc->isChecked();
        body["bt_enabled"] = m_bt->isChecked();
        if (plat == "PQ") {
            body["ext_amp_enabled"] = m_extAmp->isChecked();
            body["ext_mic_enabled"] = m_extMic->isChecked();
            body["carplay_enabled"] = m_carplay->isChecked();
            body["android_auto_enabled"] = m_aa->isChecked();
            body["mirrorlink_enabled"] = m_mlink->isChecked();
            body["carlife_enabled"] = m_carlife->isChecked();
            body["vehicle_pq"] = m_vehPq->currentData().toInt();
        } else {
            body["am_disabled"] = m_amDis->isChecked();
            body["driving_lock"] = m_dLock->isChecked();
            body["bootlogo_id"] = m_bootlogo->currentData().toInt();
        }
    }

    CodingPayload updated;
    QString err;
    if (!m_bridge.apply(body, true, updated, err)) {
        m_warnLabel->setText("Save failed: " + err);
        return;
    }
    renderPayload(updated);
    if (m_vip) {
        m_vip->sendCommands(updated.vipCmds, [this](const QStringList &replies) {
            m_msgLabel->setText("Saved & sent to VIP (" + replies.join(", ") + ")");
        });
    } else {
        m_msgLabel->setText("Saved locally");
    }
}

void CodingTab::onResetDefaults() {
    CodingPayload p;
    QString err;
    if (!m_bridge.reset(true, p, err)) {
        m_warnLabel->setText("Reset failed: " + err);
        return;
    }
    renderPayload(p);
    if (m_vip) {
        m_vip->sendCommands(p.vipCmds, [this](const QStringList &replies) {
            m_msgLabel->setText("Reset to defaults & sent to VIP (" + replies.join(", ") + ")");
        });
    }
}
```

Create `tools/rcd330_gui/src/log_tab.h` and `log_tab.cpp`:

```cpp
// log_tab.h
#pragma once

#include "vip_client.h"
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QWidget>

class LogTab : public QWidget {
    Q_OBJECT

public:
    explicit LogTab(VipClient *vip, QWidget *parent = nullptr);

private slots:
    void onLogAppended(const QString &line);
    void onSendRaw();

private:
    VipClient *m_vip = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
};
```

```cpp
// log_tab.cpp
#include "log_tab.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

LogTab::LogTab(VipClient *vip, QWidget *parent) : QWidget(parent), m_vip(vip) {
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(8);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(300);
    m_view->setObjectName("vipLog");
    vbox->addWidget(m_view, 1);

    auto *row = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Raw VIP command (e.g. PING, IGN 2, OPS 40 30 30 40)...");
    auto *sendBtn = new QPushButton("Send", this);
    auto *clearBtn = new QPushButton("Clear", this);
    connect(sendBtn, &QPushButton::clicked, this, &LogTab::onSendRaw);
    connect(m_input, &QLineEdit::returnPressed, this, &LogTab::onSendRaw);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_view->clear();
        if (m_vip) m_vip->clearLog();
    });

    row->addWidget(m_input, 1);
    row->addWidget(sendBtn);
    row->addWidget(clearBtn);
    vbox->addLayout(row);

    if (m_vip) {
        for (const QString &l : m_vip->logLines()) {
            m_view->appendPlainText(l);
        }
        connect(m_vip, &VipClient::logAppended, this, &LogTab::onLogAppended);
    }
}

void LogTab::onLogAppended(const QString &line) {
    m_view->appendPlainText(line);
}

void LogTab::onSendRaw() {
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty() || !m_vip) return;
    m_vip->sendCommand(cmd);
    m_input->clear();
}
```

Create `tools/rcd330_gui/src/main_window.h` and `main_window.cpp`:

```cpp
// main_window.h
#pragma once

#include "bezel_button.h"
#include "coding_bridge.h"
#include "coding_tab.h"
#include "log_tab.h"
#include "rfb_widget.h"
#include "vehicle_tab.h"
#include "vip_client.h"
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVector>

struct GuiConfig {
    QString vipHost = "127.0.0.1";
    quint16 vipPort = 4444;
    QString vncHost = "127.0.0.1";
    quint16 vncPort = 5900;
    QString fwName = "RCD 330";
    QString codingFile;
    QString codingScript;
    qint64 qemuPid = 0;
    bool autoConnect = true;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const GuiConfig &cfg, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isDockVisible() const;
    void toggleDock();
    CodingTab *codingTab() const { return m_codingTab; }

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateStatusBadges();
    void checkQemuLiveness();

private:
    BezelButton *makeBezelButton(const QString &label, const QString &keyName,
                                 BezelButton::HoldMode mode, QWidget *parent);

    GuiConfig m_cfg;
    VipClient m_vip;
    RfbScreenWidget *m_screen = nullptr;
    QTabWidget *m_dock = nullptr;
    QPushButton *m_dockToggleBtn = nullptr;
    QLabel *m_statusBadge = nullptr;
    VehicleTab *m_vehicleTab = nullptr;
    CodingTab *m_codingTab = nullptr;
    LogTab *m_logTab = nullptr;
    QVector<BezelButton *> m_bezelButtons;
    QTimer m_qemuWatchTimer;
    bool m_initialCodingSynced = false;
};
```

```cpp
// main_window.cpp
#include "main_window.h"
#include "rotary_knob_widget.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <csignal>

MainWindow::MainWindow(const GuiConfig &cfg, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg) {
    setWindowTitle(QString("VW RCD 330 (%1)").arg(m_cfg.fwName));

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(10);

    // Top unified RCD 330 faceplate chassis
    auto *chassis = new QFrame(central);
    chassis->setObjectName("faceplate");
    auto *faceLayout = new QHBoxLayout(chassis);
    faceLayout->setContentsMargins(12, 12, 12, 12);
    faceLayout->setSpacing(12);

    // Left bezel column
    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(10);
    leftCol->addWidget(makeBezelButton("RADIO", "RADIO", BezelButton::HoldMode::StandardHardkey, chassis));
    leftCol->addWidget(makeBezelButton("MEDIA", "MEDIA", BezelButton::HoldMode::StandardHardkey, chassis));
    leftCol->addWidget(makeBezelButton("PHONE", "PHONE", BezelButton::HoldMode::StandardHardkey, chassis));
    leftCol->addStretch(1);
    auto *volKnob = new RotaryKnobWidget("POWER / VOL", "−", "PWR", "POWER",
                                         BezelButton::HoldMode::PowerKey, "+", chassis);
    m_bezelButtons.append(volKnob->centerButton());
    connect(volKnob->centerButton(), &BezelButton::keyPressed, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 1").arg(k));
    });
    connect(volKnob->centerButton(), &BezelButton::keyReleased, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 0").arg(k));
    });
    connect(volKnob, &RotaryKnobWidget::stepped, this, [this](int delta) {
        m_vip.sendCommand(QString("ENC VOL %1").arg(delta));
    });
    leftCol->addWidget(volKnob);

    // Center: 800x480 touchscreen + sub-bezel control bar
    auto *centerCol = new QVBoxLayout();
    centerCol->setSpacing(8);
    m_screen = new RfbScreenWidget(chassis);
    centerCol->addWidget(m_screen, 0, Qt::AlignCenter);

    auto *subBar = new QHBoxLayout();
    subBar->setSpacing(8);
    auto *muteBtn = makeBezelButton("MUTE", "MUTE", BezelButton::HoldMode::InstantKey, chassis);
    muteBtn->setFixedSize(82, 34);
    auto *ejectBtn = makeBezelButton("EJECT", "EJECT", BezelButton::HoldMode::StandardHardkey, chassis);
    ejectBtn->setFixedSize(82, 34);

    m_statusBadge = new QLabel("Connecting...", chassis);
    m_statusBadge->setObjectName("statusBadge");

    m_dockToggleBtn = new QPushButton("Hide Controls ▾", chassis);
    m_dockToggleBtn->setFixedHeight(34);
    connect(m_dockToggleBtn, &QPushButton::clicked, this, &MainWindow::toggleDock);

    subBar->addWidget(muteBtn);
    subBar->addWidget(ejectBtn);
    subBar->addStretch(1);
    subBar->addWidget(m_statusBadge);
    subBar->addWidget(m_dockToggleBtn);
    centerCol->addLayout(subBar);

    // Right bezel column
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(10);
    rightCol->addWidget(makeBezelButton("VOICE", "VOICE", BezelButton::HoldMode::StandardHardkey, chassis));
    rightCol->addWidget(makeBezelButton("SETUP", "SETUP", BezelButton::HoldMode::StandardHardkey, chassis));
    rightCol->addWidget(makeBezelButton("APP", "APP", BezelButton::HoldMode::StandardHardkey, chassis));
    rightCol->addStretch(1);
    auto *tuneKnob = new RotaryKnobWidget("TUNE / OK", "◀", "OK", "TUNE_OK",
                                          BezelButton::HoldMode::StandardHardkey, "▶", chassis);
    m_bezelButtons.append(tuneKnob->centerButton());
    connect(tuneKnob->centerButton(), &BezelButton::keyPressed, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 1").arg(k));
    });
    connect(tuneKnob->centerButton(), &BezelButton::keyReleased, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 0").arg(k));
    });
    connect(tuneKnob, &RotaryKnobWidget::stepped, this, [this](int delta) {
        m_vip.sendCommand(QString("ENC TUNE %1").arg(delta));
    });
    rightCol->addWidget(tuneKnob);

    auto *leftWrap = new QWidget(chassis);
    leftWrap->setFixedWidth(134);
    leftWrap->setLayout(leftCol);
    auto *rightWrap = new QWidget(chassis);
    rightWrap->setFixedWidth(134);
    rightWrap->setLayout(rightCol);

    faceLayout->addWidget(leftWrap);
    faceLayout->addLayout(centerCol);
    faceLayout->addWidget(rightWrap);
    root->addWidget(chassis);

    // Bottom collapsible tabbed dock
    m_dock = new QTabWidget(central);
    m_vehicleTab = new VehicleTab(m_dock);
    CodingBridge bridge(m_cfg.codingScript, m_cfg.codingFile, m_cfg.fwName);
    m_codingTab = new CodingTab(bridge, &m_vip, m_dock);
    m_logTab = new LogTab(&m_vip, m_dock);

    m_dock->addTab(m_vehicleTab, "Vehicle (CAN / BAP) & Radio");
    m_dock->addTab(m_codingTab, "Headunit Coding (DID 0x0600)");
    m_dock->addTab(m_logTab, "VIP / IPCL Log");
    m_dock->setMaximumHeight(350);
    root->addWidget(m_dock);

    connect(m_vehicleTab, &VehicleTab::commandRequested, this, [this](const QString &cmd) {
        m_vip.sendCommand(cmd);
    });
    connect(m_vehicleTab, &VehicleTab::buttonTapRequested, this, [this](const QString &keyName) {
        m_vip.sendCommand(QString("BTN %1 1").arg(keyName));
        QTimer::singleShot(100, this, [this, keyName]() {
            m_vip.sendCommand(QString("BTN %1 0").arg(keyName));
        });
    });

    connect(&m_vip, &VipClient::connectionChanged, this, [this](bool ok) {
        updateStatusBadges();
        if (ok && !m_initialCodingSynced) {
            m_initialCodingSynced = true;
            m_codingTab->syncLoadedCodingToVip();
        }
    });
    connect(&m_vip, &VipClient::rebootObserved, this, [this]() {
        m_statusBadge->setText("Rebooting headunit (POWER 10s)...");
    });
    connect(m_screen, &RfbScreenWidget::connectionChanged, this, &MainWindow::updateStatusBadges);

    setCentralWidget(central);
    adjustSize();
    updateStatusBadges();

    if (m_cfg.autoConnect) {
        m_vip.connectToServer(m_cfg.vipHost, m_cfg.vipPort);
        m_screen->connectToServer(m_cfg.vncHost, m_cfg.vncPort);
    }

    if (m_cfg.qemuPid > 0) {
        m_qemuWatchTimer.setInterval(1000);
        connect(&m_qemuWatchTimer, &QTimer::timeout, this, &MainWindow::checkQemuLiveness);
        m_qemuWatchTimer.start();
    }
}

MainWindow::~MainWindow() {
    for (BezelButton *b : m_bezelButtons) {
        if (b) b->cancelHold();
    }
}

BezelButton *MainWindow::makeBezelButton(const QString &label, const QString &keyName,
                                         BezelButton::HoldMode mode, QWidget *parent) {
    auto *btn = new BezelButton(label, keyName, mode, parent);
    m_bezelButtons.append(btn);
    connect(btn, &BezelButton::keyPressed, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 1").arg(k));
    });
    connect(btn, &BezelButton::keyReleased, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 0").arg(k));
    });
    return btn;
}

bool MainWindow::isDockVisible() const {
    return !m_dock->isHidden();
}

void MainWindow::toggleDock() {
    const bool show = m_dock->isHidden();
    m_dock->setVisible(show);
    m_dockToggleBtn->setText(show ? "Hide Controls ▾" : "Show Controls ▴");
    adjustSize();
}

void MainWindow::updateStatusBadges() {
    const QString disp = m_screen->isConnected() ? "Display OK" : "Display ...";
    const QString vip = m_vip.isConnected() ? "VIP Connected" : "VIP ...";
    m_statusBadge->setText(QString("%1  |  %2").arg(disp, vip));
}

void MainWindow::checkQemuLiveness() {
    if (m_cfg.qemuPid <= 0) return;
    if (::kill(static_cast<pid_t>(m_cfg.qemuPid), 0) != 0) {
        m_qemuWatchTimer.stop();
        m_statusBadge->setText("QEMU process exited");
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    for (BezelButton *b : m_bezelButtons) {
        if (b) b->cancelHold();
    }
    m_screen->disconnectFromServer();
    m_vip.disconnectFromServer();
    QMainWindow::closeEvent(event);
}
```

Create `tools/rcd330_gui/src/main.cpp`:

```cpp
#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>

static const char *kDarkStyle = R"(
QMainWindow, QWidget {
    background-color: #0f131a;
    color: #e6edf3;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    font-size: 12px;
}
QFrame#faceplate {
    background-color: #151a22;
    border: 1px solid #262e3d;
    border-radius: 12px;
}
QWidget#knobCard {
    background-color: #10151d;
    border: 1px solid #262e3d;
    border-radius: 9px;
}
QLabel#knobTitle {
    color: #8b97a8;
    font-size: 10px;
    font-weight: 700;
}
QLabel#statusBadge {
    background-color: #10151d;
    border: 1px solid #262e3d;
    border-radius: 10px;
    padding: 4px 10px;
    color: #9bb1cc;
    font-size: 11px;
}
QLabel#warnText {
    color: #f0b429;
    font-size: 11px;
}
QLabel#mutedText {
    color: #8b97a8;
    font-size: 11px;
}
QTabWidget::pane {
    border: 1px solid #262e3d;
    border-radius: 8px;
    background-color: #151a22;
}
QTabBar::tab {
    background-color: #19202b;
    color: #9bb1cc;
    border: 1px solid #262e3d;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 6px 14px;
    margin-right: 4px;
    font-weight: 600;
}
QTabBar::tab:selected {
    background-color: #243043;
    color: #ffffff;
}
QGroupBox {
    border: 1px solid #262e3d;
    border-radius: 8px;
    margin-top: 10px;
    padding-top: 8px;
    font-weight: 700;
    color: #8b97a8;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
}
QPushButton {
    background-color: #1f2633;
    color: #e6edf3;
    border: 1px solid #323c4e;
    border-radius: 6px;
    padding: 6px 10px;
    font-weight: 600;
}
QPushButton:hover {
    background-color: #293345;
}
QPushButton:pressed {
    background-color: #161c26;
}
QPushButton:checked, QPushButton#primaryBtn {
    background-color: #1d5fd6;
    border-color: #2a7fff;
    color: #ffffff;
}
QPushButton#warnBtn {
    background-color: #4a2323;
    border-color: #7c3535;
}
QComboBox, QLineEdit {
    background-color: #10151d;
    color: #e6edf3;
    border: 1px solid #323c4e;
    border-radius: 5px;
    padding: 4px 8px;
}
QLineEdit#hexInput {
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    letter-spacing: 1px;
}
QPlainTextEdit#vipLog {
    background-color: #0b0e13;
    color: #9be09b;
    border: 1px solid #222937;
    border-radius: 6px;
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    font-size: 11px;
}
)";

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("rcd330_gui");
    app.setStyleSheet(kDarkStyle);

    QCommandLineParser parser;
    parser.setApplicationDescription("VW RCD 330 Native Control Panel & Display");
    parser.addHelpOption();

    QCommandLineOption vipHostOpt("vip-host", "VIP TCP host", "host", "127.0.0.1");
    QCommandLineOption vipPortOpt("vip-port", "VIP TCP port", "port", "4444");
    QCommandLineOption vncHostOpt("vnc-host", "QEMU VNC host", "host", "127.0.0.1");
    QCommandLineOption vncPortOpt("vnc-port", "QEMU VNC port", "port", "5900");
    QCommandLineOption fwNameOpt("fw-name", "Firmware display name", "name", "RCD 330");
    QCommandLineOption codingFileOpt("coding-file", "Persistent coding JSON path", "path", "");
    QCommandLineOption codingScriptOpt("coding-script", "Path to rcd330_coding.py", "path", "");
    QCommandLineOption qemuPidOpt("qemu-pid", "QEMU process PID to monitor", "pid", "0");

    parser.addOptions({vipHostOpt, vipPortOpt, vncHostOpt, vncPortOpt,
                       fwNameOpt, codingFileOpt, codingScriptOpt, qemuPidOpt});
    parser.process(app);

    GuiConfig cfg;
    cfg.vipHost = parser.value(vipHostOpt);
    cfg.vipPort = static_cast<quint16>(parser.value(vipPortOpt).toUInt());
    cfg.vncHost = parser.value(vncHostOpt);
    cfg.vncPort = static_cast<quint16>(parser.value(vncPortOpt).toUInt());
    cfg.fwName = parser.value(fwNameOpt);
    cfg.codingFile = parser.value(codingFileOpt);
    cfg.codingScript = parser.value(codingScriptOpt);
    cfg.qemuPid = parser.value(qemuPidOpt).toLongLong();

    if (cfg.codingScript.isEmpty()) {
        // Default relative to build/gui/rcd330_gui -> ../../tools/rcd330_coding.py
        const QDir appDir(QCoreApplication::applicationDirPath());
        cfg.codingScript = QFileInfo(appDir.filePath("../../tools/rcd330_coding.py")).absoluteFilePath();
    }
    if (cfg.codingFile.isEmpty()) {
        const QDir appDir(QCoreApplication::applicationDirPath());
        QString safeFw = cfg.fwName;
        for (QChar &ch : safeFw) {
            if (!ch.isLetterOrNumber() && ch != '.' && ch != '_' && ch != '-') ch = '_';
        }
        cfg.codingFile = QFileInfo(appDir.filePath("../coding/" + safeFw + ".json")).absoluteFilePath();
    }

    MainWindow win(cfg);
    win.show();
    win.raise();
    win.activateWindow();
    return app.exec();
}
```

Update `tools/rcd330_gui/CMakeLists.txt` to include `vehicle_tab.{h,cpp}`, `coding_tab.{h,cpp}`, `log_tab.{h,cpp}`, `main_window.{h,cpp}` in `rcd330_gui_core`, and add the `rcd330_gui` executable target (`src/main.cpp` linked against `rcd330_gui_core`).

- [ ] **Step 2: Add `MainWindow` headless smoke test (dock toggle + PQ↔MQB visibility) to `tools/rcd330_gui/tests/test_rcd330_gui.cpp`**

```cpp
    void testMainWindowDockToggleAndCodingPlatformSwitch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GuiConfig cfg;
        cfg.fwName = "5624_VW";
        cfg.codingFile = dir.filePath("mw_coding.json");
        cfg.codingScript = QDir(QStringLiteral(RCD330_REPO_ROOT)).filePath("tools/rcd330_coding.py");
        cfg.autoConnect = false;

        MainWindow win(cfg);
        win.show();
        QVERIFY(win.isDockVisible());
        win.toggleDock();
        QVERIFY(!win.isDockVisible());
        win.toggleDock();
        QVERIFY(win.isDockVisible());

        QCOMPARE(win.codingTab()->currentPlatform(), QString("PQ"));
        QVERIFY(win.codingTab()->isPqVehicleRowVisible());
        QVERIFY(!win.codingTab()->isMqbVehicleRowVisible());
    }
```

- [ ] **Step 3: Build full `rcd330_gui` binary and run all Qt6 tests**

Run: `./tools/build_gui.sh --test && ls -lh build/gui/rcd330_gui`
Expected: `build/gui/rcd330_gui` exists and is executable; all 6 Qt6 unit tests PASS under `QT_QPA_PLATFORM=offscreen`.

- [ ] **Step 4: Commit**

```bash
git add tools/rcd330_gui/CMakeLists.txt \
        tools/rcd330_gui/src/vehicle_tab.h tools/rcd330_gui/src/vehicle_tab.cpp \
        tools/rcd330_gui/src/coding_tab.h tools/rcd330_gui/src/coding_tab.cpp \
        tools/rcd330_gui/src/log_tab.h tools/rcd330_gui/src/log_tab.cpp \
        tools/rcd330_gui/src/main_window.h tools/rcd330_gui/src/main_window.cpp \
        tools/rcd330_gui/src/main.cpp tools/rcd330_gui/tests/test_rcd330_gui.cpp
git commit -m "gui: implement unified VW RCD 330 faceplate window and collapsible CAN/Coding/Log dock"
```

---

### Task 6: Launcher Integration (`tools/run_rcd330.sh`), Delete `tools/rcd330_panel.py`, Update Tests & Docs, Live Verification

**Files:**
- Modify: `tools/run_rcd330.sh`
- Delete: `tools/rcd330_panel.py`
- Modify: `tools/tests/test_bugfixes_and_coding.py`
- Modify: `tools/tests/test_plan3_rcs_boot_and_tuner.py`
- Modify: `AGENTS.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: `build/gui/rcd330_gui`, `tools/build_gui.sh`, `tools/rcd330_coding.py`, `qemu/build/qemu-system-arm`.
- Produces:
  - `./tools/run_rcd330.sh` launches QEMU (`-display none -vnc 127.0.0.1:<port - 5900>`) + `build/gui/rcd330_gui` in foreground, terminating QEMU cleanly when the native window closes.
  - `./tools/run_rcd330.sh --dry-run` prints `FW_ID`, `ZIMAGE`, `ROOTFS`, `QEMU_CMD`, and `GUI_CMD`.

- [ ] **Step 1: Update Python tests first (`test_bugfixes_and_coding.py` and `test_plan3_rcs_boot_and_tuner.py`) to expect the native GUI launcher and removal of `tools/rcd330_panel.py`**

In `tools/tests/test_bugfixes_and_coding.py`:
- Replace `test_panel_coding_api_and_persistence` with `test_native_gui_replaces_web_panel_and_wires_launcher`:

```python
    def test_native_gui_replaces_web_panel_and_wires_launcher(self):
        self.assertFalse((ROOT / "tools/rcd330_panel.py").exists())
        self.assertTrue((ROOT / "tools/build_gui.sh").is_file())
        self.assertTrue((ROOT / "tools/rcd330_gui/CMakeLists.txt").is_file())

        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertNotIn("rcd330_panel.py", run_sh)
        self.assertIn("build_gui.sh", run_sh)
        self.assertIn("rcd330_gui", run_sh)
        self.assertIn("-display", run_sh)
        self.assertIn("-vnc", run_sh)
        self.assertIn("--coding-file", run_sh)
```

In `tools/tests/test_plan3_rcs_boot_and_tuner.py`:
- Replace `test_panel_exposes_tuner_controls_and_hardens_protocol` with `test_native_gui_exposes_tuner_controls_and_hardens_vip_protocol`:

```python
    def test_native_gui_exposes_tuner_controls_and_hardens_vip_protocol(self):
        vehicle_cpp = (ROOT / "tools/rcd330_gui/src/vehicle_tab.cpp").read_text()
        vip_cpp = (ROOT / "tools/rcd330_gui/src/vip_client.cpp").read_text()
        self.assertIn("TUNER 9580 RCD 330", vehicle_cpp)
        self.assertIn("HOST_TX ", vip_cpp)
        self.assertIn("cmd.contains('\\n')", vip_cpp)
        self.assertIn("cmd.contains('\\r')", vip_cpp)
```

- [ ] **Step 2: Run Python tests to verify they fail before removing `rcd330_panel.py` and updating `run_rcd330.sh`**

Run: `python3 -m unittest discover -s tools/tests -v`
Expected: FAIL (`rcd330_panel.py` still exists, `run_rcd330.sh` still references `rcd330_panel.py`).

- [ ] **Step 3: Delete `tools/rcd330_panel.py` and update `tools/run_rcd330.sh`**

Remove `tools/rcd330_panel.py` (`rm tools/rcd330_panel.py`).

Update `tools/run_rcd330.sh`:
1. Add `GUI_BIN="$ROOT_DIR/build/gui/rcd330_gui"`, `VIP_PORT="${QEMU_VIP_PORT:-4444}"`, `VNC_PORT="${QEMU_VNC_PORT:-5900}"`, `VNC_DISPLAY=$((VNC_PORT - 5900))`.
2. In the non-dry-run preflight block, check if `$GUI_BIN` is missing or older than any source under `$ROOT_DIR/tools/rcd330_gui` (`find "$ROOT_DIR/tools/rcd330_gui" -type f -newer "$GUI_BIN"`), and run `"$ROOT_DIR/tools/build_gui.sh"` if needed.
3. Configure QEMU with:
```bash
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
    -vnc "127.0.0.1:${VNC_DISPLAY}"
    -serial stdio
    -chardev "socket,id=vipctl,host=127.0.0.1,port=${VIP_PORT},server=on,wait=off"
    -global rcd330-vip.chardev=vipctl
)
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
```
4. On `--dry-run`, print `FW_ID`, `ZIMAGE`, `ROOTFS`, `QEMU_CMD`, and `GUI_CMD`.
5. On normal launch:
```bash
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
```

- [ ] **Step 4: Update `AGENTS.md` and `README.md`**

Update `AGENTS.md` and `README.md` to:
- Replace references to `tools/rcd330_panel.py` / `http://127.0.0.1:8080` with `tools/rcd330_gui/` and `tools/build_gui.sh`.
- Document Qt6 build prerequisites (`brew install qt cmake` on macOS, `sudo apt install qt6-base-dev cmake g++` on Debian/Ubuntu).
- Document hardware button press-and-hold + right-click quick-hold shortcuts (`2.5 s` Long, `4.5 s` Service Mode, `10.0 s` Engineering Testmode, `10.5 s` `POWER` cold reboot).

- [ ] **Step 5: Run full automated test suites (Python + C++/Qt6)**

Run:
```bash
./tools/build_gui.sh --test && python3 -m unittest discover -s tools/tests -v
```
Expected: All Qt6 tests (`ctest`) AND all Python unit tests PASS.

- [ ] **Step 6: Live End-to-End QEMU Verification (`5624_vw`)**

Write and run a one-off live verification script in scratch (never committed) that:
1. Boots `qemu/build/qemu-system-arm` with `5624_vw` (`-display none -vnc 127.0.0.1:1 -chardev socket,id=vipctl,host=127.0.0.1,port=4445,server=on,wait=off`).
2. Connects a live RFB 3.8 client + VIP TCP client (and also launches `build/gui/rcd330_gui` briefly under `QT_QPA_PLATFORM=offscreen` to verify real live TCP connect + framebuffer receipt without crash).
3. Captures framebuffer PNGs and verifies visually via `read_file`:
   - Initial HMI screen booted on `CL_15`.
   - `SETUP` real 4.5 s hold (`BTN SETUP 1` -> sleep 4.5 s -> `BTN SETUP 0`) opens **Service Mode / Software Update**.
   - Enable `ENGMODE 1` + `SETUP` real 10.0 s hold opens **Engineering Testmode**.
   - `POWER` short tap (`BTN POWER 1` -> 150 ms -> `BTN POWER 0`) enters standby / clock screen, second short tap wakes back to HMI.
   - `POWER` 10.5 s hold (`BTN POWER 1` -> wait for `REBOOT POWER_10S` -> `BTN POWER 0`) triggers real cold reboot (screen blanks -> kernel + HMI reboots -> coding EEPROM retained!).

- [ ] **Step 7: Commit root repo changes (including `qemu` submodule pointer)**

```bash
git add qemu tools/run_rcd330.sh tools/tests/test_bugfixes_and_coding.py \
        tools/tests/test_plan3_rcs_boot_and_tuner.py AGENTS.md README.md
git rm tools/rcd330_panel.py
git commit -m "gui: replace web panel with unified Qt6 native window and hardware long-press support"
```
