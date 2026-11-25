[← Decompiled Firmware Index](../../README.md)

# `opt/diag` — UDS / KWP2000 Diagnostics & Coding Bridge (`5624_vw`)

- **Target binary**: `opt/diag` (`70,328` bytes, ARMv7-A Linux ELF, SHA-256 `e6ae6c9b51aacd7fbf98b9a1c369bc0f3fb17559195f7d2385ccbb33fb90f4ca`)
- **Decompiled**: `148` written / `177` total / `29` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [coding.md](../../../../coding.md) · [ipc-and-vgw.md](../../../../ipc-and-vgw.md)

## Overview

Linux-side diagnostic companion to the Renesas RH850 VIP MCU's CAN UDS/KWP2000 stack. Bridges `IPCL(0x07, 0x01)` ← `VMF 0xA5:0x02` requests from VIP and sends responses back on `VMF 0x8F:0x21` → `IPCL(0x07, 0x00)`:
- **Long Coding (`DID 0x0600`)**: Receives validated PQ (7-byte) or MQB (25-byte) coding from VIP (`actionId=5`), updates `opt/pwr_agent` (`VMF 0x0B:0x04` → `/pss1/stdplus.ini` & `PA_OUT_CODING`), persists Block `0x50` (`DIAG_PSS_CODING.pss`) via `opt/pss`, and notifies `VDiagProxy` (MQB Byte 18 bootlogo via `0xA1:0x0E`).
- **Adaptation Channels**: Dispatches PQ channels (`Ch 10/11/16/17`, `Ch 36` Threshold_High, `Ch 47` MirrorLink country code, `Ch 50` Engineering/Developer Mode → `VMF 0xA1:0x0A`) and the full MQB adaptation table (`VA 0x14cac`, including BT On/Off, PBAP priority, telephony audio dataset, HMI version, and car menu lists).
- **Actuator Tests, MVBs & DTCs**: Relays speaker-count sync (`actionId=1` → `VMF 0x73:0x13`), touchscreen/key tests, BT paired/connected device queries, and DTC status between VIP and Linux services (`0x73`, `0xA0`, `0xA1`).

## Recovered Source Files

- `src/diag_main.c`, `src/diag_core.c`, `src/diag_app.c`, `src/diag_vmf_comm.c`

## Key Functions

- [`0x8b50`](functions/0x8b50.c) — `main` / `diag_evaluate_internal_message` / `handle_diag_timeout_event`.
- [`0xabc8`](functions/0xabc8.c) — `Diag_Handle_VIP_Request`: top-level dispatcher for `IPCL(0x07, 0x01)` frames (`PQ cls6` table `@0x14124`).
- [`0x131b4`](functions/0x131b4.c), [`0x13358`](functions/0x13358.c), [`0x13544`](functions/0x13544.c) — `Diag_Init`, `Diag_Adaptions_Init`, `Diag_Adaption_Notify` (MQB table `@0x14cac`).
- [`0x13660`](functions/0x13660.c) — `Diag_Provide_Coding`: PQ/MQB coding parser, `pwr_agent` + `pss` writer, and bootlogo notifier.
- [`0xb1e8`](functions/0xb1e8.c) (`Diag_Eng_Mode_Notify`), [`0xb2e0`](functions/0xb2e0.c) (`Diag_Pmode_Notify`), [`0xc400`](functions/0xc400.c) (`Diag_Volume_control`), [`0xe028`](functions/0xe028.c) (speaker count `2`/`4` → `VMF 0x73:0x13`), [`0x10374`](functions/0x10374.c) (`Diag_BT_DSDL_TEL_Audio_Notify`), [`0x12afc`](functions/0x12afc.c) (`Diag_Handle_PSS_Read_Resp`).
