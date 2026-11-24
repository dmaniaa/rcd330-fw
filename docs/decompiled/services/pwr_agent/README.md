[← Decompiled Firmware Index](../../README.md)

# `opt/pwr_agent` — Power Manager, Watchdog & Process Supervisor (`5624_vw`)

- **Target binary**: `opt/pwr_agent` (`49,316` bytes, ARMv7-A Linux ELF, SHA-256 `ad354ce516e6d1dab644d0f5ab036332cb927ea1d82aa883547f4ad07aa831ae`)
- **Decompiled**: `227` written / `303` total / `76` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../architecture.md) · [ipc-and-vgw.md](../../../ipc-and-vgw.md) · [coding.md](../../../coding.md)

## Overview

Started directly by `/opt/launch.sh` (`pwr_agent -o -v`). Acts as **both the power/watchdog controller and the true runtime process supervisor** for the headunit (which is why most services are commented out in `/opt/launch.sh`):

- **Staged Process Supervision**: After completing the VIP power handshake (`IPCL 0x01:0x01` / `VMF 0x10:0x01`), `start_next_group_transition` ([`0xd59c`](functions/0xd59c.c)) spawns `vgw`, `pss`, `media`, `audio_proc`, `bt`, `hmi`, `diag_eol`, `diag`, `start_carplay.sh`, `connect_mgr`, `start_carlife.sh`, `exlap`, `launch_aa.sh`, and `start_mdnsd.sh` in power-state groups gated by Feature Manager flags (`0x0B:0x06`, [`0xd134`](functions/0xd134.c)).
- **VIP Watchdog & Clamps**: Feeds periodic watchdog keepalives (`VMF 0x8F:0x03` `[0x02, seq++, 0x01, vipEnabled]`) so the RH850 VIP does not power-cycle the SoC after ~60 s; translates VIP clamp/battery/power frames (`0x10:0x01`, `0x10:0x03`, `0x10:0x05`, `0x10:0x06`) into `0x0C:*` (`PA_OUT_DEV_STS`, `PA_OUT_BATT_STS`, `PA_OUT_VRM_INTO`, `PA_OUT_CODING`, startup completion `0x0C:0x0F`).
- **Early-Boot INI Sync**: Persists `[CODING] CODINGS` (`bt | (rvc << 1)`) and `[VIP]` (`PLAT`, `BRAND`, `CANSPEED`, `PARTSTR`, versions) into `/pss1/stdplus.ini`, and inspects `/sysupdate/update_info.ini`.

## Recovered Source Files

- `pwr_code/src/pwr_agent_main.c`
- `pwr_code/src/pwr_agent_transition_hdlr.c`
- `pwr_code/src/pwr_agent_vmf_comm.c`, `pwr_agent_vmf_receive.c`, `pwr_agent_vmf_send.c`
- `pwr_code/src/pwr_agent_ini.c`, `pwr_agent_app_hooks.c`

## Key Functions

- [`0x96a8`](functions/0x96a8.c) — `main` / `pwr_agent_evaluate_internal_message` / `pwr_agent_option_parse`.
- [`0xd59c`](functions/0xd59c.c) — `start_next_group_transition`: staged launch & shutdown of infotainment daemons by power group.
- [`0xd134`](functions/0xd134.c) — Feature Manager consumer (`VMF 0x0B:0x06`, gates `bt`, `carplay`, `carlife`, `exlap`, `android_auto`, etc.).
- [`0xa35c`](functions/0xa35c.c) — `pwr_agent_evaluate_vmf_POWER_AGENT_IN`: handles coding updates (`0x0B:0x04` → `/pss1/stdplus.ini` via [`0x1015c`](functions/0x1015c.c) + `PA_OUT_CODING` `0x0C:0x0D`) and HMI/service power ACKs.
- [`0xafe4`](functions/0xafe4.c) — `pwr_agent_evaluate_vmf_POWER_VIP_OUT`: handles VIP power states (`0x10:0x01`), clamps/battery (`0x10:0x03`), VRM identity (`0x10:0x05` → [`0xf704`](functions/0xf704.c)/[`0xfc58`](functions/0xfc58.c)/[`0xfd30`](functions/0xfd30.c)), and network RTC (`0x10:0x06`).
- [`0xc870`](functions/0xc870.c) — `send2vip_shutdown_complete`: signals shutdown completion (`0x8F:0x03`) to the VIP MCU.
