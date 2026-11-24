[← Decompiled Firmware Index](../README.md)

# `opt/vgw` — Vehicle Gateway (VMF ↔ SPI IPCL Bridge) (`5624_vw`)

- **Target binary**: `opt/vgw` (`23,594` bytes, ARMv7-A Linux ELF, SHA-256 `e9b10244ab5e6e0d98729f9616b9e237f5e360d44d1f3557376de13188b653d7`)
- **Decompiled**: `117` written / `146` total / `29` external / `0` failed in [pseudocode/functions/](pseudocode/functions) ([decompile-index.json](pseudocode/decompile-index.json))
- **Deep specs**: [ipc-and-vgw.md](../../ipc-and-vgw.md) · [architecture.md](../../architecture.md)

## Overview

Vehicle Gateway daemon launched early by `opt/pwr_agent` (and directly by `/etc/rc.d/rcS` during firmware update mode). Terminates the SPI IPCL link to the Renesas RH850 VIP MCU (`libHAL.so.1.0.0`: `spi_ipcl_init`/`read`/`write`) and bridges it onto the `opt/vmf` publish/subscribe bus using modular Bus Interface (`BIF`) adapters:

1. **Main VMF RX Thread**: Subscribes to VMF groups **`0x8F`**, **`0x8D`**, and **`0x7C`**, maps `(vmf_grp, vmf_sub)` through a **44-entry TX routing table**, and transmits IPCL frames to the VIP MCU via `spi_ipcl_write`.
2. **SPI IPCL RX Thread (`ipcl_receive_thread`)**: Reads frames from the RH850 VIP via `spi_ipcl_read`, translates `(ipcl_grp, ipcl_sub)` through a **36-entry RX routing table**, and publishes VMF notifications (`0x10`, `0x34`, `0x50`, `0x6E`, `0x6F`, `0x70`, `0x74`, `0xA2`, `0xA5`, etc.) for `opt/pwr_agent`, `opt/hmi`, `opt/audio_proc`, and `opt/diag`.
3. **Fast-Path Power Thread (`0x8E`)**: Directly forwards power-clamp/standby handshakes `(0x8E, 0xD9)` → `IPCL(0x8F, 0xFE, len=4)`.

## Recovered Source Files

- `src/nw_gw_main.c`
- `src/nw_gw_msg_queue.c`
- `src/bif/vmf_bif/nw_gw_vmf_bif.c`

## Key Functions

- [`0x8b78`](pseudocode/functions/0x8b78.c) — `main`: initializes OSAL, message queues, and application BIFs (`vmf_bif`, `ipcl_r013_78k0_bif`, `debug_bif`).
- [`0x8fd0`](pseudocode/functions/0x8fd0.c) — `nw_vgw_basic_message_receiption`: VMF message reception callback routing messages into the gateway queue.
- [`0x90dc`](pseudocode/functions/0x90dc.c) / [`0x91b4`](pseudocode/functions/0x91b4.c) / [`0x924c`](pseudocode/functions/0x924c.c) — `nw_gw_msg_q_write`, `nw_gw_msg_q_read_wait`, `nw_gw_msg_q_init`: semaphore-backed internal message queue.
- [`0x92f8`](pseudocode/functions/0x92f8.c) / [`0x9330`](pseudocode/functions/0x9330.c) — `nw_vgw_init_application_bifs`, `nw_vgw_open_application_bifs`.
- [`0x935c`](pseudocode/functions/0x935c.c) / [`0x944c`](pseudocode/functions/0x944c.c) / [`0x94a8`](pseudocode/functions/0x94a8.c) — `ipcl_r013_78k0_bif_write`, `ipcl_r013_78k0_bif_open`, `ipcl_receive_thread`: SPI IPCL transport endpoints.
- [`0x979c`](pseudocode/functions/0x979c.c) / [`0x96b0`](pseudocode/functions/0x96b0.c) — `nw_gw_register_bif`, `nw_gw_write_bif`: generic BIF registration and dispatch.
