[← Decompiled Firmware Index](../../README.md)

# `opt/bt` — ALPS Bluetooth Stack & Vocle ECNR Service (`5624_vw`)

- **Target binary**: `opt/bt` (`680,932` bytes, ARMv7-A Linux ELF, SHA-256 `f1fdf228ed2887489d2d18946eebb1f3f4a02498a27e1937a37750113538c68e`)
- **Decompiled**: `1,525` written / `1,681` total / `156` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../architecture.md) · [coding.md](../../../coding.md) · [Android Auto ECNR Echo Bug](../../../bugs/android-auto-echo-ecnr.md)

## Overview

Bluetooth stack daemon controlling the **ALPS C210** UART Bluetooth module (and its integrated **Vocle** voice/ECNR DSP) via `libcidb_sdk.so`:
- **Profiles & Persistence**: Manages pairing (`/pss2/app_data/bt_paired_devs_list`), HFP hands-free calls, PBAP phonebook download/storage/search (`/pss2/app_data/pb.db`), MAP messaging, A2DP/AVRCP media streaming, and MQB BT coding mirror (`/pss2/app_data/bt_coding.config`, default `0x2F`).
- **VMF Interface**: Receives commands from `opt/hmi` (`VBTProxy`), `opt/diag`, `opt/audio_proc`, and projection apps on groups `0x1E`, `0x22`, `0xCC`; publishes connection state, call state, phonebook, A2DP metadata, and mode completion events (`0x20:0x8D`, `0x20:0x90`) on `0x1F`, `0x20`, `0x26`, `0x27`.
- **ALPS Vocle ECNR & Projection Audio Switching**: Handles `VMF 0x22:0x88 [mode]` to switch the ALPS module between internal HFP SCO (`0x0343 00`) and external PCM/I2S (`0x0343 01`, `hw:0,4`) for CarPlay Siri/Call, MirrorLink VR, and Android Auto VR (`mode=5`, which is the **only** mode sending `0x0331 00` to turn ECNR off — see [android-auto-echo-ecnr.md](../../../bugs/android-auto-echo-ecnr.md)).

## Recovered Source Files

- `src/bt_vmf_interface/bt_vmf_interface.c`
- `src/bt_function/`: `bt_function_core.c`, `bt_function_config.c`, `bt_function_phone.c`, `bt_function_audio.c`, `bt_function_message.c`, `bt_function_interface.c`
- `src/bt_function/bt_pim/`: `bt_pim.c`, `pbdl/bt_pim_pbdl.c`, `db/bt_pim_storage.c`, `db/bt_pim_sort_search.c`
- `src/bt_os/bt_os.c`

## Key Functions

- [`0xd12c`](functions/0xd12c.c) — `main`: service startup and ALPS module initialization.
- [`0x11970`](functions/0x11970.c) / [`0xb504`](functions/0xb504.c) — `tmMI_VMsgHandler` / `tm_InHMIEventHandler`: main VMF RX dispatchers (`0x22:0x88` → `0x285e8`, `0x22:0x94` → `0x450f8`).
- [`0x28d80`](functions/0x28d80.c) — `BtConfigInitSM`: boot-time ALPS initialization sequence (enables `0x0331 01` Voice/ECNR, loads ECNR params `0x0332` and telephony dataset `0x0345`).
- [`0x271b8`](functions/0x271b8.c), [`0x273b8`](functions/0x273b8.c), [`0x277dc`](functions/0x277dc.c), [`0x287a0`](functions/0x287a0.c) — ALPS UART command builders (`0x0331` Voice/ECNR, `0x0343` audio path, `0x0332` ECNR params, `0x0345` telephony audio dataset).
- [`0x285e8`](functions/0x285e8.c) & [`0x2adb8`](functions/0x2adb8.c) — `tm_fun_config_set_carplay_mode` & ALPS CFM state machine (`ReceiveAlpsSetCodecSrcCfm`, `ReceiveAlpsSetAudioPathCfm`, `BtConfigVoiceSetModeCfm`).
- [`0x25188`](functions/0x25188.c), [`0x24000`](functions/0x24000.c), [`0x28c10`](functions/0x28c10.c), [`0x450f8`](functions/0x450f8.c) — HFP audio path / SCO transfer (`ReceiveAlpsAudioPathInd`), A2DP disconnect `FailMode` retry, and AA connection status (`tm_fun_core_aa_connect_status`).
- [`0x14a5c`](functions/0x14a5c.c), [`0x879d8`](functions/0x879d8.c), [`0x87b3c`](functions/0x87b3c.c) — MQB Coding Byte 16 (`g_ucBtCoding`) read/write against PSS Block `0x50` and `/pss2/app_data/bt_coding.config`.
