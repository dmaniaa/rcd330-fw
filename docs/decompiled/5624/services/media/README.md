[← Decompiled Firmware Index](../../README.md)

# `opt/media` — Local Media Playback & Indexing Service (`5624_vw`)

- **Target binary**: `opt/media` (`384,008` bytes, ARMv7-A Linux ELF, SHA-256 `6ac7213b4f27ffdd30692211dd8e90f86262c2bfb2e5b802b2f76a92c2a76abf`)
- **Decompiled**: `1,155` written / `1,303` total / `148` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md) · [ipc-and-vgw.md](../../../../ipc-and-vgw.md)

## Overview

Media management, file indexing, and playback controller for USB storage, SD cards, AUX, and legacy iPod audio (`/dev/ipodout0`), built around the RACE media indexing framework (`/tmp/race/database`, `/tmp/race/mount/base`, `/tmp/race/iPOD_logos`):
- **VMF Interface**: Receives play/pause/seek/folder-browse/source commands from `opt/hmi` (`VMediaProxy`) on groups `0x28` and `0x29`; publishes track metadata, playback progress, album-art references, and USB/SD/AUX device status on `0x2A`.
- Also interfaces with `opt/bt` for Bluetooth A2DP playback state and with `opt/diag_eol` for media EOL checks.

## Recovered Source Files

- `media_code/media_src/media_app_task/media_app_task.c`

## Key Functions

- [`0xad98`](functions/0xad98.c) — `main`: service startup and media task initialization.
- [`0x16248`](functions/0x16248.c), [`0x1a584`](functions/0x1a584.c), [`0x21644`](functions/0x21644.c), [`0x36c0c`](functions/0x36c0c.c), [`0x45b60`](functions/0x45b60.c) — Largest core media state machines, playlist/database browsing, and VMF event dispatchers.
- [`0x2d0a0`](functions/0x2d0a0.c), [`0x2da98`](functions/0x2da98.c), [`0x2dd88`](functions/0x2dd88.c) — `media_app_debug_agent`, `media_app_iPod_debug_agent`, `media_app_sys_debug_agent`.
