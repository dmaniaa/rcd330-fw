[← Decompiled Firmware Index](../../README.md)

# `opt/lib/libOSAL.so.1.0.0` — OS Abstraction & Logging Library (`5624_vw`)

- **Target binary**: `opt/lib/libOSAL.so.1.0.0` (`55,656` bytes, ARMv7-A shared library, SHA-256 `8f349666c02aa2a778e9e031dd39260e1e977006e623f1dc8712696e5f1f3d3f`)
- **Decompiled**: `396` written / `523` total / `127` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md)

## Overview

Common POSIX OS abstraction layer linked by nearly all Visteon infotainment services (`opt/hmi`, `opt/vgw`, `opt/pwr_agent`, `opt/pss`, `opt/diag`, `opt/audio_proc`, `opt/bt`, etc.). Provides:
- **Threads, mutexes, events, barriers, and internal message queues** (`os_task_*`, `os_mutex_*`, `os_event_*`, `os_barrier_*`, `os_message_queue_*`)
- **Millisecond and tick timers** (`os_timer_create`, one-shot/periodic timers, `os_delay_milliseconds`)
- **Structured trace logging** (`os_trace.cpp` → `printlog`, `log_optional`, `log_simple`), configured per-service via `/pss1/config/<service>.conf`
- **Embedded `minIni`** INI parser/writer (`util/minINI/minIni.c`) used across services for `/pss1/stdplus.ini`, `/pss1/config/*.conf`, and `/opt/feature_config.ini`
- **Process/memory diagnostics** reading `/proc/%ld/stat`, `/proc/%d/cmdline`, and `/tmp/mem`

## Recovered Source Files

- `src/os_init.c`, `src/os_internal.c`, `src/os_task.c`, `src/os_timer.c`
- `src/os_mutex.c`, `src/os_message.c`, `src/os_event.c`, `src/os_barrier.c`
- `src/os_trace.cpp`, `src/os_statistics.c`
- `util/minINI/minIni.c`

## Key Exported Functions

- **Lifecycle & Stats**: [`0x139b0`](functions/0x139b0.c) (`os_init`), [`0x13c2c`](functions/0x13c2c.c) (`os_destroy`), [`0x13d10`](functions/0x13d10.c) (`os_statistics`), [`0x14b64`](functions/0x14b64.c) (`os_assert`).
- **Timers**: [`0x1412c`](functions/0x1412c.c) (`os_timer_process_init`), [`0x141e8`](functions/0x141e8.c) (`os_timer_create`), [`0x14410`](functions/0x14410.c) (`os_timer_start_one_shot_milliseconds`), [`0x14444`](functions/0x14444.c) (`os_timer_start_periodic_milliseconds`), [`0x14550`](functions/0x14550.c) (`os_timer_cancel`), [`0x147a0`](functions/0x147a0.c) (`os_delay_milliseconds`).
- **Message Queues**: [`0x14c90`](functions/0x14c90.c) (`os_message_queue_create`), [`0x14dd8`](functions/0x14dd8.c) (`os_message_queue_link`), [`0x14ea0`](functions/0x14ea0.c) (`os_message_queue_send`), [`0x1506c`](functions/0x1506c.c) (`os_message_queue_get`).
