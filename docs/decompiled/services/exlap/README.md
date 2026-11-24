[← Decompiled Firmware Index](../../README.md)

# `opt/exlap` — VW Exlap Vehicle Telemetry Server (`5624_vw`)

- **Target binary**: `opt/exlap` (`751,740` bytes, ARMv7-A Linux ELF, SHA-256 `3e3bc1d7cbf4bc7f180601cf604495b2f3baf4273884aca13757575f364d7587`)
- **Decompiled**: `1,977` written / `2,212` total / `235` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../architecture.md) · [coding.md](../../../coding.md)

## Overview

Volkswagen **Exlap** (Extensible Lightweight Application Protocol) telemetry server spawned by `opt/pwr_agent` (gated by Feature Manager `EXLAP_ATTR`, index `6`). Exposes vehicle state, coding identity, and CAN/BAP telemetry from `opt/vmf` to connected smartphone apps (e.g., MirrorLink / VW companion apps) over TCP/XML, with logging configured via `/opt/exlap_clog.conf` (`zlog`):
- Reads 25-byte MQB Long Coding from PSS Block `0x50` (`DIAG_PSS_CODING.pss`) to populate `Car_Brand`, `Car_Class`, `Car_Generation`, `Car_Derivative`, `Car_Variant` (Bytes `0..2`), and startup screen identity (Byte `18`).

## Recovered Source Files

- `src/app.c`, `src/car.c`, `src/vmf_if.c`, `src/nw_adapter.c`
- Embedded `zlog` logging library (`zlog.c`, `rule.c`, `conf.c`, `rotater.c`, `category.c`, `format.c`, `thread.c`, `buf.c`)

## Key Functions

- [`0xc76c`](functions/0xc76c.c) — `main`: initializes `zlog`, VMF interface, and Exlap protocol server.
- [`0xd1c8`](functions/0xd1c8.c) & [`0xd4e4`](functions/0xd4e4.c) — PSS Block `0x50` coding reader and MQB vehicle identity decoder (`Car_Brand`..`Car_Variant`).
- [`0xeaa8`](functions/0xeaa8.c), [`0x1cf70`](functions/0x1cf70.c) — Core VMF/vehicle-data dispatcher and Exlap protocol engine.
- [`0x11634`](functions/0x11634.c) — `exlapIfLogMessage`.
