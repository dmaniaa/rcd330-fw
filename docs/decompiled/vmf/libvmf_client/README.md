[← Decompiled Firmware Index](../../README.md)

# `opt/lib/libvmf_client.so` — VMF Client IPC Library (`5624_vw`)

- **Target binary**: `opt/lib/libvmf_client.so` (`57,880` bytes, ARMv7-A shared library, SHA-256 `dcb110b772a38b0dc7b3e044e1252fcad92cc83a588a226e3a17074dacc938f5`)
- **Decompiled**: `212` written / `258` total / `46` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [ipc-and-vgw.md](../../../ipc-and-vgw.md)

## Overview

Client-side shared library linked by every VMF-speaking service (`opt/hmi`, `opt/vgw`, `opt/pwr_agent`, `opt/pss`, `opt/audio_proc`, `opt/bt`, `opt/media`, `opt/diag`, `opt/sysupdate`, projection daemons, and `vmf_send`/`vmf_rcv`). Implements client registration with `opt/vmf` (`/tmp/127.0.0.1`), group subscription (`nw_vmf_register_msg_group`), blocking/timed receive, synchronous & asynchronous basic packet transmit (`nw_vmf_send_basic`), and internal VMF timers.

## Recovered Source Files

- `vmf/03_code/src/nw_vmf_client.c`
- `vmf/03_code/src/nw_vmf_collector.c`

## Key Exported Functions

- **Connection & Subscriptions**: [`0x1397c`](functions/0x1397c.c) (`nw_vmf_client_init`), [`0x139ac`](functions/0x139ac.c) (`nw_vmf_connect`), [`0x147c8`](functions/0x147c8.c) (`nw_vmf_connect_async`).
- **Low-Level IPC Channels**: [`0x11c54`](functions/0x11c54.c) (`nw_vmf_ipc_init`), [`0x11d74`](functions/0x11d74.c) (`nw_vmf_ipc_open`), [`0x12020`](functions/0x12020.c) (`nw_vmf_ipc_create`), [`0x12318`](functions/0x12318.c) (`nw_vmf_ipc_create_async`), [`0x124f0`](functions/0x124f0.c) (`nw_vmf_ipc_send`), [`0x12920`](functions/0x12920.c) (`nw_vmf_ipc_receive`), [`0x129c0`](functions/0x129c0.c) (`nw_vmf_ipc_timed_receive`), [`0x12fac`](functions/0x12fac.c) (`nw_vmf_ipc_create_channel_name`).
- **Collector & Timers**: [`0x1887c`](functions/0x1887c.c) (`nw_vmf_co_client_add_msg`), [`0x193d4`](functions/0x193d4.c) (`vmf_timer_start`).
