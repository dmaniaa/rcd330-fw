[← Decompiled Firmware Index](../../README.md)

# `opt/vmf` — Visteon Message Framework Broker Daemon (`5624_vw`)

- **Target binary**: `opt/vmf` (`155,892` bytes, ARMv7-A Linux ELF, SHA-256 `9a6df837744e01f682b4e8e1d56fb51cadfa0180df67a206f5c1182d5e262064`)
- **Decompiled**: `431` written / `518` total / `87` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [ipc-and-vgw.md](../../../../ipc-and-vgw.md) · [architecture.md](../../../../architecture.md)

## Overview

Central publish/subscribe IPC message router started first by `/opt/launch.sh` (and in `/etc/rc.d/rcS` update mode). Maintains the in-memory group subscription routing database (`nw_vmf_routing_db-core.c` / `nw_vmf_routing_db-memory.c`), accepts client registrations over Unix domain sockets / System V IPC (`/tmp/127.0.0.1`, `/tmp/vmf_trace_startup`), routes 26-byte-header VMF packets by `groupId` (`pkt[0x14]`) to all subscribed clients, and collects system/trace telemetry (`/proc/meminfo`, `/proc/stat`, `/proc/uptime`, `/proc/sys/net/unix/max_dgram_qlen`, `nw_vmf_router_spy_queue.c`).

## Recovered Source Files

- `vmf/03_code/src/nw_vmf_client.c`
- `vmf/03_code/src/nw_vmf_collector.c`
- `vmf/03_code/src/nw_vmf_router_spy_queue.c`
- `vmf/03_code/src/nw_vmf_routing_db-core.c`
- `vmf/03_code/src/nw_vmf_routing_db-memory.c`

## Key Functions

- [`0x9ee4`](functions/0x9ee4.c) — `main`: daemon startup, socket/IPC setup, and router initialization.
- [`0xde38`](functions/0xde38.c) & [`0xec54`](functions/0xec54.c) — Internal VMF client connection setup (shared codebase with `libvmf_client.so`'s `nw_vmf_connect` / `nw_vmf_connect_async`).
- [`0x1c0a4`](functions/0x1c0a4.c), [`0x20790`](functions/0x20790.c), [`0x2311c`](functions/0x2311c.c) — Core message routing, collector, and routing-database handlers.
