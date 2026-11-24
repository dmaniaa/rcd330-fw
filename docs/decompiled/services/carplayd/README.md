[← Decompiled Firmware Index](../../README.md)

# `opt/carplayd` — Apple CarPlay Core Session Daemon (`5624_vw`)

- **Target binary**: `opt/carplayd` (`83,340` bytes, ARMv7-A Linux ELF, SHA-256 `92b66daeca6e9bf689e2b1ced947c5aed9fc5dc20a3def5a24033df9e36607bc`)
- **Decompiled**: `397` written / `549` total / `152` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Related**: [carplay_App/README.md](../carplay_App/README.md) · [libCarPlayAudio/README.md](../libCarPlayAudio/README.md) · [libCarPlayPlatformSystem/README.md](../libCarPlayPlatformSystem/README.md) · [iap2d/README.md](../iap2d/README.md)

## Overview

Apple CarPlay core session daemon launched by `opt/start_carplay.sh`. Daemonizes, initializes the platform layer via `libCarPlayPlatformSystem.so` (`CarPlaySystemInit`), starts the POSIX CarPlay IPC server (`CarPlayIPCServerInitialize`), and enters a CoreFoundation `CFRunLoopRun()` serving `opt/carplay_App`. Manages AirPlay/CarPlay session setup, H.264 video streaming, audio stream lifecycles (via `libCarPlayAudio.so`), HID/touch forwarding, and property queries.

## Recovered Source Files

- `Plugin/Interface/CarPlayIPCServer.c` (from vendor build tree `carplay_std_R5_CD32_Nov11`)

## Key Functions

- [`0xae18`](functions/0xae18.c) — `main`: calls `daemon(0, 1)`, `CarPlaySystemInit()`, `CarPlayIPCServerInitialize` ([`0x111f0`](functions/0x111f0.c)), and `CFRunLoopRun()`.
- [`0xccf0`](functions/0xccf0.c), [`0xe860`](functions/0xe860.c) — Core CarPlay IPC request and session state dispatchers.
- [`0x16bb4`](functions/0x16bb4.c), [`0x16e0c`](functions/0x16e0c.c), [`0x16efc`](functions/0x16efc.c) — `CarPlayHandleServerCopyProperty`, `CarPlayHandleSessionCreated`, `CarPlayHandleSessionFailed`.
