[← Decompiled Firmware Index](../../README.md)

# `opt/diag_eol` — End-of-Line (Factory) Test Service (`5624_vw`)

- **Target binary**: `opt/diag_eol` (`19,568` bytes, ARMv7-A Linux ELF, SHA-256 `c28eee25dbf349a34816e99fd3f9f6b7cb4a836d3919170251f5634817c5a368`)
- **Decompiled**: `85` written / `108` total / `23` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../architecture.md) · [coding.md](../../../coding.md)

## Overview

Factory End-of-Line (EOL) manufacturing test daemon spawned by `opt/pwr_agent`. Bridges VIP EOL commands (`opt/vgw`) onto VMF to exercise hardware subsystems on the assembly line:
- Audio test tones & routing (`diag_eol_deal_audio_handler`)
- Media / USB / SD checks (`diag_eol_deal_CDOther_media_handler`)
- Physical faceplate button & bezel checks (`diag_eol_deal_button_bezel_test_handler`)
- MirrorLink & Bluetooth verification (`diag_eol_deal_mirrorlink_handler`, `diag_eol_bluetooth_deal_message_response`)
- Direct PSS block read/write (`diag_eol_pss_read_request` / `diag_eol_pss_write_request`) and `/opt/feature_config.ini` inspection

## Recovered Source Files

- `src/diag_eol_main.c`, `src/diag_eol_state.c`
- `src/diag_eol_vmf_comm.c`, `src/diag_eol_rcv.c`, `src/diag_eol_vmf_send.c`

## Key Functions

- [`0x8988`](functions/0x8988.c) — `main`: initializes OSAL, EOL state machine, and VMF thread.
- [`0x8be8`](functions/0x8be8.c) — `diag_eol_vmf_comm_thread`: VMF receive loop.
- [`0x9178`](functions/0x9178.c) / [`0x92ac`](functions/0x92ac.c) — `diag_eol_send_message`, `diag_eol_send_ipcl_msg`.
- [`0x9468`](functions/0x9468.c) / [`0x96d4`](functions/0x96d4.c) — `diag_eol_pss_write_request`, `diag_eol_pss_read_request`.
- [`0x9f20`](functions/0x9f20.c) (`audio`), [`0xa3f4`](functions/0xa3f4.c) (`media`), [`0xa81c`](functions/0xa81c.c) (`button_bezel`), [`0xa988`](functions/0xa988.c) (`mirrorlink`), [`0xabfc`](functions/0xabfc.c) (`vgw`), [`0xadfc`](functions/0xadfc.c) (`hmi`), [`0xb198`](functions/0xb198.c) (`audio_resp`), [`0xb2b8`](functions/0xb2b8.c) (`bt_resp`).
