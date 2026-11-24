# Native Unified Qt6 Control Panel & Hardware Long-Press Support — Design Spec

**Date:** 2026-11-24  
**Scope:** Replace `tools/rcd330_panel.py` (browser HTTP control panel) with a cross-platform (macOS + Linux) native **Qt6 Widgets** application (`tools/rcd330_gui/`) that embeds QEMU's 800×480 display inside a unified VW RCD 330 bezel window, adds true press-and-hold and quick-hold long-press hardware button semantics, and upgrades QEMU's `rcd330-vip` device model for `POWER` 10 s cold reboot and non-volatile coding EEPROM persistence across resets.

---

## 1. Goals & Non-Goals

### Goals
1. **Single Native Window (`rcd330_gui`):** Launch automatically alongside QEMU from `tools/run_rcd330.sh` on both macOS and Linux, combining the virtual VW RCD 330 faceplate, embedded live 800×480 touchscreen, and a collapsible tabbed controls dock (**Vehicle CAN/BAP & Radio**, **Headunit Coding**, **VIP Log**).
2. **True Long-Press Support for Hardware Buttons:**
   - Real mouse press-and-hold (`BTN <KEY> 1` on press, `BTN <KEY> 0` on release) with live hold duration & milestone progress feedback, plus stuck-key safety release on focus loss, window hide, app exit, or TCP disconnect.
   - Right-click quick-hold presets aligned with verified `opt/hmi` and RH850 VIP firmware thresholds (`2.0 s` Long, `4.0 s` Service Mode / Extended Long, `9.0 s` Engineering Testmode / Ultra-Long, and `10.0 s` `POWER` cold reboot).
3. **QEMU VIP Hold & Reset Fidelity:**
   - Upgrade `BTN POWER 1`/`0` in `qemu/hw/misc/rcd330_vip.c` so short press (<10 s on release) toggles standby/clock while a 10.0 s continuous hold triggers `qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)`.
   - Preserve configured VIP coding/VRM EEPROM fields across resets (matching real RH850 Data Flash) and clear the framebuffer to black in `imx6_ipu_reset`.
4. **Single Source of Truth for Coding:** Keep all VW Long Coding bitfield encoding, decoding, `vipImage.bin 0x1f48a` validation, JSON persistence, and VIP command generation inside `tools/rcd330_coding.py` (shared by `tools/build_rootfs.sh`, `tools/rcd330_gui`, and `tools/tests/`).
5. **Remove Browser Panel:** Delete `tools/rcd330_panel.py` completely and update scripts, docs, and tests.

### Non-Goals
- No Chromium / QtWebEngine dependency (`Qt6::Widgets` + `Qt6::Network` only).
- No audio streaming or USB host-device passthrough in this change.
- No multi-touch RFB extension (single-finger capacitive touch matches `atmel_mxt.c`'s finger-0 T100 model).

---

## 2. Firmware Facts Driving the Design

Verified directly from `docs/decompiled/hmi/functions/` (`5624_vw`):
- **Hardkey state machine (`VIPCLNotifyFilter`):**
  - `0xaa77c` (`VKeyDriver::readKeyDriver`) reads raw `EV_KEY` (`value=1` press, `value=0` release) from `/dev/event0` and forwards to `0x42fdc` (`HandleLPCLHardKey`).
  - `0x4398c` initializes the hardkey manager (`0x3bc10`) with base interval `2000 ms` (`FUN_0003bd90(..., 2000)`) and registers press/release callbacks (`0x437d0` `hard_key_long_press`, `0x432f0`/`0x435c8` `hard_key_extend_long_press`, `0x43200` `hard_key_ultralong_press`).
  - `0x3c564` ticks held keys through the exact cumulative thresholds:
    - **< 2.0 s (release):** Short press.
    - **2.0 s:** `hard_key_long_press` (para[3]=`5`).
    - **4.0 s (`2000 + 2000 ms`):** `hard_key_extend_long_press` (para[3]=`7`) — in `HandleLPCLHardKeySet` (`0x42088`), `SETUP` opens **Service Mode / Version & Software Update** (`0x11`).
    - **9.0 s (`2000 + 2000 + 5000 ms`):** `hard_key_ultralong_press` (para[3]=`9`) — in `HandleLPCLHardKeySet` (`0x42088`), `SETUP` opens **Engineering Testmode / Green Menu** (`0x0d`) when `eng_mode == 1`.
- **VIP `POWER` knob:** Wired directly to the Renesas RH850 VIP MCU (not `/dev/event0`). Short press toggles standby/clock (`s->pwr_off`); holding for 10 seconds triggers a cold reboot of the i.MX6 SoC.
- **Display & Touch:** `imx6_ipu.c` renders an 800×480 32bpp XRGB `DisplaySurface` with dirty-row tracking; `atmel_mxt.c` consumes QEMU absolute pointer events (`INPUT_EVENT_KIND_ABS` + `INPUT_BUTTON_LEFT`), which QEMU's built-in VNC server forwards directly from RFB `PointerEvent`.

---

## 3. System Architecture & Lifecycle

```
┌────────────────────────────────────────────────────────────────────────────┐
│                        tools/run_rcd330.sh                                 │
│  1. Ensures QEMU, staged rootfs, and build/gui/rcd330_gui are up to date   │
│  2. Starts QEMU in background (-display none -vnc 127.0.0.1:<vnc_port>,    │
│     -chardev socket,id=vipctl,host=127.0.0.1,port=<vip_port>, -serial stdio)│
│  3. Runs build/gui/rcd330_gui in foreground (passing ports, fw-name,       │
│     coding-file, coding-script, --qemu-pid)                                │
│  4. Closing the window exits rcd330_gui -> EXIT trap kills QEMU cleanly    │
└────────────────────────┬──────────────────────────────┬────────────────────┘
                         │ RFB 3.8 (127.0.0.1:5900)     │ VIP Line TCP (127.0.0.1:4444)
                         ▼                              ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                 build/gui/rcd330_gui (C++20 / Qt6 Widgets)                 │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │ Left Bezel  │   800×480 RfbScreenWidget (RFB 3.8)   │  Right Bezel   │  │
│  │ RADIO/MEDIA │   Direct 1:1 Touch -> atmel_mxt       │  VOICE/SETUP/  │  │
│  │ PHONE/VOL   │   [MUTE] [EJECT] [Status] [Hide Dock] │  APP/TUNE-OK   │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │ Collapsible Tabbed Dock:                                             │  │
│  │  [Vehicle (CAN/BAP) & Radio] | [Headunit Coding] | [VIP / IPCL Log]  │  │
│  └─────────────────────────────────┬────────────────────────────────────┘  │
└────────────────────────────────────┼───────────────────────────────────────┘
                                     │ Subprocess JSON CLI (load / apply / reset)
                                     ▼
                         tools/rcd330_coding.py  ──►  build/coding/<FW_ID>.json
                         (shared with build_rootfs.sh & tools/tests/)
```

### Build & Directory Layout
- **`tools/rcd330_gui/`**
  - `CMakeLists.txt` — C++20, `find_package(Qt6 REQUIRED COMPONENTS Widgets Network)`, optional `Test` target (`BUILD_TESTING=ON`), builds `rcd330_gui` executable + `rcd330_gui_core` static library (so unit tests link directly against all non-`main` classes).
  - `src/main.cpp` — Argument parsing, dark automotive QSS theme, `MainWindow` launch.
  - `src/rfb_protocol.{h,cpp}` — Pure, socket-independent RFB 3.8 state machine & pixel buffer decoder (`Raw` + `CopyRect` + `DesktopSize` pseudo-encoding, 32bpp little-endian XRGB directly into `QImage::Format_RGB32`) for clean unit testing.
  - `src/rfb_widget.{h,cpp}` — `QWidget` owning a `QTcpSocket` + `RfbSession`, auto-reconnect timer (250 ms backoff), exact 1:1 coordinate mapping (`0..799 × 0..479`), `mousePressEvent`/`mouseMoveEvent`/`mouseReleaseEvent` -> RFB `PointerEvent`, and connection status overlay.
  - `src/vip_client.{h,cpp}` — Async reconnecting `QTcpSocket` client for `127.0.0.1:4444`, command queue with sequential line dispatch (`<CMD>\n` -> reply line, rejecting embedded `\r`/`\n` to prevent command injection and properly buffering partial TCP reads until `\n`), 1.5 s `PING` keepalive/status check, startup & post-reconnect coding sync trigger, and ring-buffered log (last 200 entries).
  - `src/bezel_button.{h,cpp}` — Hold-aware button widget implementing real press/release, live elapsed timer & progress fill, firmware threshold markers, right-click quick-hold context menu, early cancel, and safety auto-release.
  - `src/rotary_knob_widget.{h,cpp}` — Compact `-` / center `BezelButton` / `+` composite widget with mouse-wheel support (`wheelEvent` emits `+1`/`-1` encoder steps).
  - `src/coding_bridge.{h,cpp}` — Thin `QProcess` wrapper calling `python3 tools/rcd330_coding.py {load,apply,reset}` and parsing the JSON response into typed structs.
  - `src/vehicle_tab.{h,cpp}` — Ignition clamp, Gear/RVC, Speed/RPM/Fuel/Temp sliders, Doors, OPS parking presets, Clock sync, MFSW SWC buttons, FM/RDS station presets.
  - `src/coding_tab.{h,cpp}` — All PQ (7-byte) & MQB (25-byte) coding widgets, PQ↔MQB visibility switching, raw hex editor, live `validation_warnings` banner, `Save & Apply Live`, `Apply Raw Hex`, `Reset Defaults`.
  - `src/log_tab.{h,cpp}` — Monospace read-only VIP/IPCL/CAN log view, Clear button, and raw VIP command input line.
  - `src/main_window.{h,cpp}` — Unified faceplate + collapsible bottom dock + QEMU liveness watcher (monitors `--qemu-pid` via `kill(pid, 0)` every 1 s so unexpected QEMU exit updates the banner cleanly).
  - `tests/test_rcd330_gui.cpp` — Qt Test (`QTest`) suite run headless with `QT_QPA_PLATFORM=offscreen`.
- **`tools/build_gui.sh`** — Configures & builds `tools/rcd330_gui` into `build/gui/` (staging binary at `build/gui/rcd330_gui`), auto-detecting Homebrew Qt6 (`brew --prefix qt` / `/opt/homebrew/opt/qt` / `/usr/local/opt/qt`) on macOS and standard CMake paths on Linux. Uses Ninja if installed, else Unix Makefiles. Supports `--test` to run `ctest --output-on-failure` under `QT_QPA_PLATFORM=offscreen`.
- **`tools/run_rcd330.sh`** —
  - Keeps existing CLI flags (`[<firmware.tar>]`, `--fw`, `--dry-run`, `--rebuild`, `--coding-file`, `--sd`).
  - Auto-builds `build/gui/rcd330_gui` if missing or if any file under `tools/rcd330_gui/` is newer than the binary (skipped on `--dry-run`).
  - Resolves `VIP_PORT` (default `4444`, env `QEMU_VIP_PORT`) and `VNC_PORT` (default `5900`, env `QEMU_VNC_PORT`; passes `-vnc 127.0.0.1:<VNC_PORT - 5900>` and `-display none`).
  - Launches QEMU in the background keeping `-serial stdio` attached to the terminal, records `QEMU_PID`, sets an `EXIT INT TERM` trap to terminate QEMU gracefully (`kill $QEMU_PID`) when the GUI exits, and runs `build/gui/rcd330_gui` in the foreground.
  - Wait on `--dry-run`: prints `FW_ID`, `ZIMAGE`, `ROOTFS`, `QEMU_CMD`, and `GUI_CMD` without building or launching.

---

## 4. Detailed Component Specifications

### 4.1 RFB 3.8 Client (`rfb_protocol` + `rfb_widget`)
- **Handshake:**
  1. Receive server version (`RFB 003.008\n` or `003.007`), reply `RFB 003.008\n`.
  2. Receive security types list, select type `1` (`None` — local `127.0.0.1` loopback only), check 4-byte `SecurityResult == 0`.
  3. Send `ClientInit` (`shared = 1`).
  4. Receive `ServerInit` (width, height, server pixel format, desktop name); allocate `QImage(w, h, QImage::Format_RGB32)` (800×480).
  5. Send `SetPixelFormat`: 32 bpp, depth 24, `big-endian = 0`, `true-color = 1`, `red-max = 255` (`shift = 16`), `green-max = 255` (`shift = 8`), `blue-max = 255` (`shift = 0`). This matches `QImage::Format_RGB32` (`0xFFRRGGBB` in host little-endian memory) 1:1 on both arm64 and x86_64 macOS/Linux — raw scanlines copy directly via `memcpy`!
  6. Send `SetEncodings`: `[0 (Raw), 1 (CopyRect), -223 (DesktopSize)]`.
  7. Send initial non-incremental `FramebufferUpdateRequest(0, 0, w, h)`, then request incremental updates immediately upon finishing each `FramebufferUpdate` (keeping the pipe saturated at QEMU's refresh cadence with zero polling lag).
- **Pointer / Touch Input:**
  - `RfbScreenWidget` enforces a minimum & preferred size of `800×480` (with crisp 1:1 display by default, or aspect-ratio-preserving coordinate transform if scaled).
  - `mapToFramebuffer(QPointF widgetPos)` computes integer `(x, y)` clamped to `[0, fbWidth - 1] × [0, fbHeight - 1]`, independent of `devicePixelRatio` (Qt widget coordinates are logical pixels, matching the widget's logical `rect()`).
  - `mousePressEvent` (LeftButton) sends `PointerEvent(mask=1, x, y)`.
  - `mouseMoveEvent` while LeftButton held sends `PointerEvent(mask=1, x, y)` (throttled only if identical pixel).
  - `mouseReleaseEvent` (LeftButton) sends `PointerEvent(mask=0, x, y)`.
  - `focusOutEvent` / disconnect clears active touch state (`mask=0`) if pressed.
- **Reconnect Handling:**
  - On `QAbstractSocket::errorOccurred` or `disconnected`, reset protocol state, show "Reconnecting to QEMU display…", and retry every 250 ms until QEMU's VNC socket accepts.

### 4.2 Hardware Button Long-Press Widget (`BezelButton`)
- **Configuration per button:**
  - `keyName`: VIP button token (`RADIO`, `MEDIA`, `PHONE`, `VOICE`, `SETUP`, `APP`, `TUNE_OK`, `EJECT`, `MUTE`, `POWER`).
  - `HoldMode`:
    - `StandardHardkey`: milestones at `2.0 s` ("Long"), `4.0 s` ("Service"), `9.0 s` ("Eng"); right-click menu offers:
      - `Short Tap (100 ms)`
      - `Long Press (2.5 s)`
      - `Service Hold (4.5 s)`
      - `Ultra-Long Hold (10.0 s)`
    - `PowerKey`: milestone at `10.0 s` ("Reboot"); right-click menu offers:
      - `Short Tap — Standby / Wake (100 ms)`
      - `Hard Reboot Hold (10.5 s)`
    - `InstantKey` (for `MUTE` which pulses SWC `0x5d`): still sends `BTN MUTE 1` on press / `BTN MUTE 0` on release, with `Short Tap` only (or optional hold feedback).
- **State Machine:**
  - `Idle` → left mouse press → `ManualHold`: emits `pressedKey(keyName)` (`BTN <key> 1`), starts `QElapsedTimer` + 50 ms UI repaint timer.
  - `ManualHold` → left mouse release (or `focusOutEvent` / `hideEvent` / destructor) → `Idle`: emits `releasedKey(keyName)` (`BTN <key> 0`), stops timer.
  - `Idle` → right-click quick-hold(durationMs) → `ScriptedHold(targetMs)`: emits `pressedKey(keyName)` (`BTN <key> 1`), starts `QElapsedTimer` + single-shot `QTimer(targetMs)` + 50 ms UI repaint timer.
  - `ScriptedHold` → timer fires OR left-click cancel OR `hideEvent`/destructor → `Idle`: stops timer, emits `releasedKey(keyName)` (`BTN <key> 0`).
- **Visual Feedback (`paintEvent`):**
  - Clean dark automotive styling (matching VW RCD 330 dark charcoal matte faceplate with subtle red VW accent border when active).
  - During `ManualHold` or `ScriptedHold`: draws a subtle bottom progress fill bar across the button (`elapsed / maxMilestone`, turning amber at ≥2 s, VW red at ≥4 s / ≥9 s / reboot) and displays the live elapsed time (e.g. `SETUP  4.2s`) directly inside the button.

### 4.3 `tools/rcd330_coding.py` JSON CLI Subcommands
Add three non-breaking subcommands alongside existing `seed`:
- `python3 tools/rcd330_coding.py load --coding-file <path> --fw-id <id>`
  - Loads (or initializes defaults for) `<path>` and prints a single JSON object to stdout:
    ```json
    {
      "config": { ... },
      "decoded": { ... },
      "vip_cmds": [ "VRM ...", "OPS_CFG ...", "SPEAKERS ...", "ENGMODE ...", "CODING_SYNC ..." ],
      "coding_file": "/abs/path/to/coding.json",
      "pq_vehicles": { "1": "VW321", ... },
      "mqb_vehicles": { "50": "VW313", ... },
      "mqb_bootlogos": { "0": "ASIA — Normal VW", ... }
    }
    ```
- `python3 tools/rcd330_coding.py apply --coding-file <path> --fw-id <id> [--live]`
  - Reads JSON update dict from `stdin`, calls `apply_coding_update` + `save_coding_config`, and prints the same payload schema with `vip_cmds` using `CODING` (when `--live` is passed) or `CODING_SYNC`.
- `python3 tools/rcd330_coding.py reset --coding-file <path> --fw-id <id> [--live]`
  - Resets to `default_coding_config(fw_id)`, saves to `--coding-file`, and prints the same JSON payload.

This guarantees 100% parity between `rcd330_gui`, `build_rootfs.sh`, and Python tests without duplicating a single line of bitfield or CRC-16 logic in C++.

### 4.4 QEMU Device Model Upgrades

#### `qemu/hw/misc/rcd330_vip.c`
1. **New state fields in `Rcd330VipState`:**
   - `QEMUTimer *pwr_hold_timer;`
   - `bool pwr_btn_down;`
   - `bool pwr_reboot_fired;`
   - `uint16_t held_keys_mask;` (tracks currently depressed `/dev/event0` button codes `1..10` so disconnect releases any stuck keys cleanly!)
2. **Non-volatile EEPROM initialization (`vip_realize` vs `vip_reset_hold`):**
   - Move one-time initialization of persistent coding/VRM EEPROM fields (`platform`, `brand`, `vehicle`, `can_speed`, `part_num`, `hw_ver`, `serial_num`, `coding`, `coding_len`, `speakers`, `eng_mode`, `ops_sensors`) into `vip_realize`.
   - Keep `vip_reset_hold` resetting all volatile runtime state (`startup_synced = false`, `ign_state = 2`, `pwr_off = 0`, `rev_gear = 0`, `pwr_btn_down = false`, `pwr_reboot_fired = false`, `held_keys_mask = 0`, deleting `srq_timer`, `knob_timer`, `pwr_hold_timer`, clearing SPI/key queues, resetting tuner/audio transient state).
3. **`POWER` 10 s hold & cold reboot (`#include "system/runstate.h"`):**
   - Create `s->pwr_hold_timer = timer_new_ms(QEMU_CLOCK_REALTIME, vip_pwr_hold_timer_cb, s);` in `vip_realize`.
   - `vip_pwr_hold_timer_cb`: if `s->pwr_btn_down`, sets `s->pwr_reboot_fired = true`, `s->pwr_btn_down = false`, calls `vip_notify_host(s, "REBOOT POWER_10S\n")`, and calls `qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)`.
   - `BTN POWER 1`: if `!s->pwr_btn_down`, sets `s->pwr_btn_down = true`, `s->pwr_reboot_fired = false`, arms `timer_mod(s->pwr_hold_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 10000)`.
   - `BTN POWER 0`: calls `timer_del(s->pwr_hold_timer)`. If `s->pwr_btn_down && !s->pwr_reboot_fired`, toggles `s->pwr_off = !s->pwr_off` and calls `vip_send_power_state(s)`. Sets `s->pwr_btn_down = false`, `s->pwr_reboot_fired = false`.
4. **Hardkey tracking & disconnect cleanup (`vip_chr_event`):**
   - Register `vip_chr_event` in `qemu_chr_fe_set_handlers`.
   - On `BTN <NAME> 1`/`0` for `/dev/event0` codes `1..10`, update bit `(1u << code)` in `s->held_keys_mask`.
   - On `CHR_EVENT_CLOSED`, cancel `s->pwr_hold_timer`, clear `s->pwr_btn_down`/`s->pwr_reboot_fired`, and enqueue release (`value=0`) for every bit set in `s->held_keys_mask`, then clear `s->held_keys_mask`.

#### `qemu/hw/display/imx6_ipu.c`
- In `imx6_ipu_reset`: if `s->con` has a `DisplaySurface`, zero the surface framebuffer (`memset(surface_data(surface), 0, surface_stride(surface) * surface_height(surface))`) and call `qemu_console_update(s->con, 0, 0, surface_width(surface), surface_height(surface))` so a `POWER` 10 s reboot immediately blanks the VNC display before the kernel boots back up.

---

## 5. Error Handling & Edge Cases

| Scenario | Behavior |
|---|---|
| QEMU boots slower than GUI | `RfbScreenWidget` and `VipClient` retry every 250–500 ms without modal error dialogs; coding sync runs automatically on first `PONG`. |
| User holds `SETUP` or `POWER` and alt-tabs / closes window | `BezelButton` emits `BTN <KEY> 0` on `focusOutEvent`, `hideEvent`, and destructor; QEMU's `CHR_EVENT_CLOSED` also releases all tracked held keys and cancels `pwr_hold_timer`. |
| User drags mouse off a held `BezelButton` before releasing | Qt delivers `mouseReleaseEvent` to the pressed widget regardless of cursor position, cleanly sending `BTN <KEY> 0`. |
| `POWER` 10 s reboot fires while GUI is open | QEMU resets in place; VNC and VIP TCP connections stay up (or auto-reconnect if reset), display blanks to black then shows VW boot splash, and VIP retains coding EEPROM without resetting to defaults. |
| Coding validation warnings (e.g. invalid PQ raw hex) | Shown prominently in the amber warning banner on the Coding tab, matching `validate_pq_coding()` (`vipImage.bin 0x1f48a`). |
| VNC port 5900 already in use | Configurable via `QEMU_VNC_PORT` environment variable (and `QEMU_VIP_PORT` for VIP TCP). |
| Missing Qt6 on host during `build_gui.sh` | Fails fast with clear one-line install instructions (`brew install qt cmake` on macOS, `sudo apt install qt6-base-dev cmake g++` on Debian/Ubuntu). |

---

## 6. Verification & Testing Plan

1. **C++ / Qt6 Headless Unit Tests (`./tools/build_gui.sh --test`, `QT_QPA_PLATFORM=offscreen`):**
   - `RfbSession`: handshake parsing, `Raw` and `CopyRect` rect decoding into `QImage::Format_RGB32`, `DesktopSize` resize, and touch coordinate clamping across HiDPI/scaled geometries.
   - `VipClient`: newline-injection rejection (`\r`/`\n`), partial TCP reply buffering, sequential command dispatch, and disconnect cleanup.
   - `BezelButton`: manual press/release emission order, scripted quick-hold auto-release timing, early click-to-cancel, and focus-loss safety release.
   - `CodingBridge`: end-to-end `load` / `apply` / `reset` round-trip through `tools/rcd330_coding.py` in a temporary directory.
2. **Python Unit Tests (`python3 -m unittest discover -s tools/tests -v`):**
   - Update `tools/tests/test_bugfixes_and_coding.py` and `tools/tests/test_plan3_rcs_boot_and_tuner.py` to remove deleted `rcd330_panel.py` imports/checks and add tests for:
     - `tools/rcd330_coding.py` `load`, `apply`, and `reset` CLI subcommands.
     - `qemu/hw/misc/rcd330_vip.c` `POWER` 10 s hold timer (`pwr_hold_timer`, `qemu_system_reset_request`), `CHR_EVENT_CLOSED` stuck-key release, and EEPROM preservation across `vip_reset_hold`.
     - `qemu/hw/display/imx6_ipu.c` blank-on-reset.
     - `tools/run_rcd330.sh` `--dry-run` outputting `-display none -vnc` and `rcd330_gui` invocation (and confirming `tools/rcd330_panel.py` is removed).
3. **Live QEMU + Firmware Integration Verification (`5624_vw`):**
   - Build QEMU (`./tools/build_qemu.sh`) and GUI (`./tools/build_gui.sh --test`).
   - Boot `5624_vw` via QEMU (`-display none -vnc 127.0.0.1:...` + VIP TCP), verify via live RFB framebuffer capture:
     - `SETUP` 4.5 s hold opens **Service Mode / Software Update**.
     - `SETUP` 10.0 s hold (with `eng_mode=true`) opens **Engineering Testmode**.
     - `POWER` short tap (100 ms) toggles standby/clock screen.
     - `POWER` 10.5 s hold triggers a real cold reboot (screen blanks -> kernel + HMI reboots -> coding preserved).
     - `rcd330_gui` connects cleanly to live QEMU VNC + VIP, renders the full unified faceplate + dock without layout errors, and cleanly terminates QEMU when closed.
4. **Documentation Sync:** Update `AGENTS.md` and `README.md` to document `tools/rcd330_gui/`, `tools/build_gui.sh`, Qt6 prerequisites on macOS/Linux, and long-press shortcuts.
