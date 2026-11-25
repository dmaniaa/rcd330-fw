# IPC (`opt/vmf`) & Vehicle Gateway (`opt/vgw`) Protocol

All infotainment services communicate over the Visteon Message Framework (**VMF**) daemon (`opt/vmf`) using `libvmf_client.so` (and optional `libvmf_trace.so`). Vehicle CAN traffic never reaches Linux directly; it passes through the **Renesas RH850 VIP MCU** over SPI IPCL (`libHAL.so.1.0.0`, `/dev/spidev0.0` + `/dev/v850srq` + GPIO 83), terminated by `opt/vgw`.

---

## 1. VMF Wire Framing (`VBaseProxy::PackMsg` @ [`0x5fe68`](decompiled/5624/hmi/functions/0x5fe68.c))

Every VMF packet starts with a fixed **26-byte (`0x1A`) header** followed immediately by payload bytes (up to `0x4B4` payload bytes in a `0x4D0`-byte buffer):

| Offset | Size | Type | Value / Meaning |
|---:|---:|---|---|
| `0x00` | 1 | `uint8_t` | `0x02` (constant frame start byte) |
| `0x01..0x13` | 19 | `uint8_t[19]` | Zero / internal `libvmf_client.so` routing metadata |
| `0x14` (20) | 1 | `uint8_t` | **Group ID** (e.g. `0x8F`, `0x8D`, `0x8E`, `0x7C`, `0xA2`, `0x3F`, `0x70`) |
| `0x15` (21) | 1 | `uint8_t` | **Sub / Event ID** (command/notification code within group) |
| `0x16..0x17` | 2 | `uint16_t` | Zero / reserved |
| `0x18` (24) | 2 | `uint16_t` LE | **Payload length** (`len`) |
| `0x1A` (26) | `len` | `uint8_t[]` | Payload buffer |

Inside `opt/hmi`, `VBaseProxy::PackMsg` ([`0x5fe68`](decompiled/5624/hmi/functions/0x5fe68.c)) accepts an internal 4-byte prefix `[grp, sub, len_hi, len_lo]` followed by `len` payload bytes, converts the big-endian length `(buf[2] << 8) | buf[3]` into little-endian `*(uint16_t*)(pkt + 0x18)`, copies `buf + 4` to `pkt + 0x1a`, and transmits via `VmfThread::send` ([`0x606e8`](decompiled/5624/hmi/functions/0x606e8.c) -> `nw_vmf_send`).

For CLI testing on the target or inside QEMU, [`opt/tools/vmf_send`](decompiled/5624/vmf/vmf_send) and [`opt/tools/vmf_rcv`](decompiled/5624/vmf/vmf_rcv) inject and monitor decimal VMF packets (`vmf_send <dec_group> <dec_sub> [dec_byte0 ...]`).

---

## 2. `opt/vgw` VMF <-> SPI IPCL Bridge ([docs/decompiled/5624/vgw/pseudocode/](decompiled/5624/vgw/pseudocode))

`opt/vgw` registers with `opt/vmf` using `libvmf_client.so` and runs three threads:
1. **Main VMF RX Thread**: Subscribes to VMF groups **`[0x8f, 0x8d, 0x7c]`**, looks up `(vmf_grp, vmf_sub)` in a **44-entry TX routing table** ([`0x8b78`](decompiled/5624/vgw/pseudocode/functions/0x8b78.c)..[`0xa320`](decompiled/5624/vgw/pseudocode/functions/0xa320.c)), translates the header, and calls `spi_ipcl_write` in `libHAL.so.1.0.0`.
2. **SPI IPCL RX Thread**: Reads frames via `spi_ipcl_read` ([`0x1439c`](decompiled/5624/hal_osal/libHAL/functions/0x1439c.c)) from the Renesas RH850 VIP MCU, maps `(ipcl_grp, ipcl_sub)` through a **36-entry RX routing table**, and publishes VMF events back onto the bus for `opt/hmi`, `opt/pwr_agent`, and `opt/diag`.
3. **Power Thread (`0x8e`)**: Dedicated fast-path forwarding `(0x8e, 0xd9) -> (0x8f, 0xfe, len=4)` power management handshakes.

### SPI IPCL Frame Wire Format (`libHAL.so.1.0.0`)
Each VIP -> SoC IPCL frame transferred over `/dev/spidev0.0` (with chip-select / handshake on GPIO 83 and interrupt notifications on `/dev/v850srq`) has the structure:
```text
[total_hi, total_lo, sub_len_hi, sub_len_lo, ipcl_grp, ipcl_sub, payload[0..plen-1], crc16_hi, crc16_lo]
```
where `sub_len = 4 + plen`, `total = 2 + sub_len + 2 = 8 + plen`, and `crc16` is CRC-16/CCITT-FALSE (`init = 0xFFFF`, `poly = 0x1021`) over the preceding `6 + plen` bytes.

---

## 3. Complete VMF Group ID & Inter-Service Routing Matrix

Derived from `VBaseProxy::RegisterVmfGroup` ([`0x5fdd4`](decompiled/5624/hmi/functions/0x5fdd4.c)) and `VBaseProxy::SendVmfMsg` ([`0x5ff4c`](decompiled/5624/hmi/functions/0x5ff4c.c)) across all 18 `opt/hmi` subsystem proxies and peer services:

| VMF Group (Hex / Dec) | Direction | Producer -> Consumer | Purpose |
|---|---|---|---|
| `0x0B` (`11`) | `HMI -> pwr_agent` | `VPowerProxy` -> `opt/pwr_agent` | HMI power state acknowledgments, backlight, user power requests |
| `0x0C` (`12`), `0x0D` (`13`), `0x0E` (`14`) | `pwr_agent -> HMI` | `opt/pwr_agent` -> `VPowerProxy` | Power mode notifications, `PA_OUT_DEV_STS` (`0x0C:0x04` `CL_S`/`CL_15`), startup completion (`0x0C:0x0F`) |
| `0x10` (`16`) | `vgw -> pwr_agent` | `opt/vgw` -> `opt/pwr_agent` | VIP power state (`0x10:0x01`), clamp status (`0x10:0x03`), network RTC (`0x10:0x06`) |
| `0x1E` (`30`), `0x22` (`34`) | `HMI -> bt` | `VBTProxy` -> `opt/bt` | Bluetooth dial, answer/hangup, pairing, phonebook sync, A2DP transport commands |
| `0x1F` (`31`), `0x20` (`32`), `0x26` (`38`), `0x27` (`39`) | `bt -> HMI` | `opt/bt` -> `VBTProxy`, `VMediaProxy`, `VAndroidAutoProxy` | Bluetooth connection state, HFP call status, phonebook/call logs, A2DP metadata |
| `0x28` (`40`), `0x29` (`41`) | `HMI -> media` | `VMediaProxy` -> `opt/media` | Media play/pause/seek/folder browse/source selection commands |
| `0x2A` (`42`) | `media -> HMI` | `opt/media` -> `VMediaProxy` | Media track metadata, playback progress, USB/SD/AUX device status |
| `0x3C` (`60`), `0x3E` (`62`), `0x47` (`71`) | `HMI -> audio_proc` | `VAudioProxy`, `VMirrorLinkProxy` -> `opt/audio_proc` | Audio channel switch, volume/mute request, bass/mid/treble EQ, fader/balance |
| `0x3D` (`61`), `0x3F` (`63`), `0x45` (`69`) | `audio_proc -> HMI` | `opt/audio_proc` -> `VAudioProxy`, `VCarLifeProxy`, `VMirrorLinkProxy` | Primary volume status (`0x3F:0x00`), mute status, active audio focus/channel grant |
| `0x6E` (`110`), `0x74` (`116`) | `vgw/pwr -> HMI` | `opt/vgw`, `opt/pwr_agent` -> `VPowerProxy` | VIP audio mode status (`0x74:0x01`), power/wake notifications |
| `0x6F` (`111`) | `vgw -> HMI` | `opt/vgw` -> `VAMFMProxy`, `VServiceProxy` | AM/FM tuner frequency, RDS PS/RT, signal strength, station list updates |
| `0x70` (`112`), `0x7B` (`123`), `0xA2` (`162`) | `vgw -> HMI` | `opt/vgw` -> `VBAPProxy`, `VSystemTimeCalendarProxy` | CAN/BAP vehicle telemetry (speed, RPM, exterior temp, reverse gear, doors, OPS, MFA, climate) |
| `0x71` (`113`) | `HMI -> services` | `VSubsystemProxy`, `VMediaProxy` -> services | Current tuner band (`0x71:0x09`) & audio source (`0x71:0x0A`) broadcast |
| `0x72` (`114`) / `0x8C` (`140`) | `pss <-> HMI` | `opt/pss` <-> `VPSSProxy`, `VAppConnectProxy` | Read/write persistent `/pss1` & `/pss2` blocks (`0x8C` request, `0x72` response) |
| `0x73` (`115`), `0xA1` (`161`) / `0xA0` (`160`) | `diag <-> HMI` | `opt/diag`, `opt/diag_eol` <-> `VDiagProxy`, `VBAPProxy` | UDS / EOL diagnostic requests, DTC logging, coding & adaptation responses |
| `0x81` (`129`), `0xC2` (`194`), `0xC3` (`195`) | `carplay_App <-> HMI` | `opt/carplay_App` <-> `VCarPlayProxy` | Apple CarPlay screen resource requests, touch/key forwarding, audio focus, ducking |
| `0x83` (`131`), `0xC7` (`199`) | `carlife_daemon <-> HMI` | `opt/carlife_daemon` <-> `VCarLifeProxy` | Baidu CarLife session lifecycle, video/audio focus, touch forwarding |
| `0x8D` (`141`), `0xC8` (`200`), `0xC9` (`201`) | `connect_mgr <-> HMI` | `opt/connect_mgr` <-> `VAppConnectProxy`, `VCarPlayProxy`, `VAndroidAutoProxy`, `VCarLifeProxy`, `VMirrorLinkProxy` | USB device enumeration, role switching (`usbroleswitch`), active projection protocol selection |
| `0x8E` (`142`) | `pwr_agent -> vgw` | `opt/pwr_agent` -> `opt/vgw` | Fast-path power clamp / standby handshake (`0x8E:0xD9` -> IPCL `0x8F:0xFE`) |
| `0x8F` (`143`) | `HMI/services -> vgw` | `opt/hmi`, `opt/pwr_agent`, `opt/sysupdate` -> `opt/vgw` | Outbound commands to Renesas RH850 VIP MCU over SPI IPCL (`0x8F:0x03` watchdog, `0x8F:0x19` update reset, `0x8F:0x22` tuner, `0x8F:0x24` BAP/clock, `0x8F:0x27` VIP audio, `0x8F:0x2E` USB/backlight, `0x8F:0x30` BAP extra) |
| `0xBD` (`189`) / `0xBE` (`190`) | `HMI <-> sysupdate` | `VServiceProxy` <-> `opt/sysupdate` | Software update discovery, confirmation, progress, and version validation |
| `0xC4` (`196`), `0xD2` (`210`) / `0xA4` (`164`) | `MirrorLink <-> HMI` | `libmlc_iface.so` <-> `VMirrorLinkProxy` | MirrorLink UPnP/VNC session state, app listing, drive-lock popups |
| `0xDC` (`220`) / `0xDD` (`221`) | `AAP_Application <-> HMI` | `opt/AndroidAuto/bin/AAP_Application` <-> `VAndroidAutoProxy` | Android Auto video/audio focus, touch/key input (`0xDD`), status notifications (`0xDC`) |

---

## 4. Key Verified VMF <-> SPI IPCL Messages

| Direction | VMF `(Group, Sub)` | IPCL `(Group, Sub)` | Purpose |
|---|---|---|---|
| `pwr_agent -> VIP` | `(0x8F, 0x01)` | `(0x01, 0x01)` | Initial power agent startup handshake |
| `pwr_agent -> VIP` | `(0x8F, 0x03)` | `(0x01, 0x03)` | Watchdog keepalive `[0x02, seq++, 0x01, vipEnabled]`, start complete `[0x04]`, shutdown complete `[0x05]` |
| `sysupdate -> VIP` | `(0x8F, 0x19)` | `(0x0B, 0x02)` | 1-byte status; `1` disables watchdog and hard-resets SoC |
| `audio_proc <-> VIP` | `(0x8F, 0x1F)` / `(0x50, 0x02)` | `(0x03, 0x00)` / `(0x03, 0x02)` | VIP DSP commands (`0x18` PSS read, `0x16` DSP run, `0x00` source connect, `0x01` mute, `0x02` volume, `0x17` PSS write) & ACKs (`+0x40`) |
| `diag <-> VIP` | `(0x8F, 0x21)` / `(0xA5, 0x02)` | `(0x07, 0x00)` / `(0x07, 0x01)` | UDS/KWP2000 diagnostic sync (`actionId=1` speakers, `actionId=5` Long Coding, `actionId=6` KWP info, `actionId=7` Ch 50 EngMode) |
| `HMI -> VIP` (`VAMFMProxy`) | `(0x8F, 0x22)` | `(0x05, 0x01)` | AM/FM tuner seek, manual tune (`0x37`), band switch (`0x30`), preset recall (`0x65`), seek mode (`0x4E`) |
| `HMI -> VIP` (`VBAPProxy`) | `(0x8F, 0x24)` | `(0x0C, 0x01)` | BAP HMI startup (`0x57` -> ACK `0x69`), BAP HMI exit (`0x72` -> ACK `0x6B`), vehicle settings, OPS/RVC popup control (`0x49`), climate, MFA reset, and cluster clock/time sync |
| `HMI <-> VIP` (`VAudioProxy` / `VPowerProxy`) | `(0x8F, 0x27)` / `(0x74, 0x01)` | `(0x08, 0x03)` / `(0x08, 0x02)` | Audio mode save/get (`0x01`/`0x02` -> `0x81`), backlight (`0x04`/`0x05`), battery voltage query (`0x13` -> `0x8B` `[0x8B, hw_state, volt_dV_hi, volt_dV_lo]` displayed as `%u.%u V` in ETM), hardware eCall mute pin (`0x19` -> `0x90`, `0`=inactive, `1`=eCall), volume knob (`0x8C`), SWC keys (`0x83`) |
| `pwr_agent <-> VIP` | `(0x8F, 0x2B)` / `(0x10, 0x05)` | `(0x0E, 0x01)` / `(0x0E, 0x02)` | VRM platform/brand/vehicle/part-number (`0x01` -> `0x02`, 17 B) & HW part/version/serial (`0x03` -> `0x04`, 29 B) |
| `HMI -> VIP` (`VPowerProxy`) | `(0x8F, 0x2E)` | `(0x01, 0x0C)` | USB power reset, display power control |
| `Power -> VIP` | `(0x8E, 0xD9)` | `(0x8F, 0xFE)` | Power clamp / standby handshake (`len = 4`) |
| `VIP -> pwr_agent` | `(0x10, 0x01)` | `(0x01, 0x01)` | Power handshake `[1]` and power state `[3, mode, reason, acc]` (`2=STANDBY`, `3=RUN`, `6=TEMP_ON`, `7=OFF_USER`) |
| `VIP -> pwr_agent -> HMI` | `(0x10, 0x03)` -> `(0x0C, 0x04)` / `(0x0C, 0x05)` | `(0x01, 0x03)` | `[4, hw_state, volt_cV_hi, volt_cV_lo, 0, 0]` (`PA_OUT_BATT_STS`, `0x0C:0x05`); `[5, b0, b1, b2, b3]` (`PA_OUT_DEV_STS`, `0x0C:0x04`): `b0 & 3` = `m_ucACCStatus` (`CL_S`), `(b0 >> 2) & 3` = `m_ucIgnitionStatus` (`CL_15`) |
| `VIP -> pwr_agent` | `(0x10, 0x06)` | `(0x06, 0x07)` | Network RTC clock `[year-2000, month, day, hour, min, sec, fmt]` |
| `VIP -> HMI` (`VBAPProxy`) | `(0xA2, 0x00)` | `(0x06, 0x00)` | Vehicle speed (BE `uint16` in `0.01 km/h`) & RPM (BE `uint16`) |
| `VIP -> HMI` (`VBAPProxy`) | `(0xA2, 0x02)` | `(0x06, 0x03)` | Handbrake / park-brake status flag (`vbap+0x230`, `vipImage.bin` `0x2d6e4`) |
| `VIP -> HMI` (`VBAPProxy`) | `(0xA2, 0x03)` | `(0x06, 0x04)` | Clamp states `[CL_S (0/1), CL_15 (0/1), CL_58 (0/1)]` (`vipImage.bin` `0x2d29c` -> `vbap+0x231..0x233`, read by ETM `VETMSwCarMainPage` `0x23cc20`) |
| `VIP -> HMI` (`VBAPProxy`) | `(0xA2, 0x05)` | `(0x06, 0x06)` | `[0]` PQ ambient light sensor (`0..253`, `254`=hold, `255`=fault), `[1]` PQ ext temp (`(C + 50) * 2`), `[2]` temp unit (`0`=°C/`1`=°F), `[3]` PQ temp invalid (`0`/`1`) / MQB `CL_58d` (`0..100`), `[4]` MQB `CL_58s` (`0..100`), `[5]` night mode (`0`/`1` -> CarPlay/AA/MirrorLink), `[6]` RHD (`0`/`1`) (`vipImage.bin` `0x2d554`) |
| `VIP -> HMI` (`VBAPProxy`) | `(0xA2, 0x06)` | `(0x06, 0x08)` | 38 B car/BEM info (`vipImage.bin` `0x2d360` -> `VBAPProxy` `0xb71f4`): `[0..1]` CAN language, `[2]` BEM Stage 1 low-battery warning (popup `0x6B` "Attention: 12V battery low..."), `[3]` BEM Stage 2 critical shutdown (popup `0x64` "12V battery almost drained..."), `[5]` trunk open, `[6]` reverse gear, `[7..37]` CAN/BAP stack versions (`V03.70.03` / `V03.05.01`) |
| `VIP -> HMI` (`VBAPProxy`) | `(0xA2, 0x09)` | `(0x06, 0x0C)` | Net-to-HMI status: `[1, sc_state]` (`MSG_NET2AP_HMI_SC_STATE`, `0`=Safe Code unlocked, `1`=locked), `[2, deliver]` (`MSG_NET2AP_HMI_DELIVER_STATE`), `[3, odo_b2..0]` (odometer), `[4, fl, fr, rl, rr, trunk, hood]` (doors) |
| `VIP -> HMI` (`VBAPProxy`) | `(0x70, 0x01)` | `(0x0C, 0x02)` | BAP OPS distances (`0x01..0x04`), sensor count (`0x0C`), `CarIfPopupCmd_Acknowledge` (`0x4B`), MQB temp (`0x55`), Car Menu adaptation (`0x70`) |
| `VIP -> HMI` (`VBAPProxy`) | `(0x34, 0x01)` | `(0x0C, 0x04)` | BAP Climatronic status |
| `audio_proc -> HMI` (`VAudioProxy`) | `(0x3F, 0x01)` | — | `AUDIO_AM_PRIMARY_VOLUME_STATUS` 6 B `[0x00, v_act, v_ent, v_phone, v_ring, v_sds]` (`0..30`) & `AUDIO_AM_HMI_MUTE_STATUS` 5 B `[0x04, active, ent, phone, ring]` |

Full route tables and raw SPI reader/writer implementations are preserved in [`docs/decompiled/5624/vgw/pseudocode/functions/`](decompiled/5624/vgw/pseudocode/functions) and [`docs/decompiled/5624/hal_osal/libHAL/functions/0x1439c.c`](decompiled/5624/hal_osal/libHAL/functions/0x1439c.c).
