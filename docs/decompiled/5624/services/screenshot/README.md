[← Decompiled Firmware Index](../../README.md)

# `opt/screenshot` — Framebuffer Screenshot Capture Service (`5624_vw`)

- **Target binary**: `opt/screenshot` (`55,684` bytes, ARMv7-A Linux ELF, SHA-256 `47778064412f683523d52f30cd2d102c32de7af5ef635e72fd584a5e6bb594ff`)
- **Decompiled**: `399` written / `544` total / `145` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md)

## Overview

Qt 4.7.4 (`QCoreApplication`) VMF service capable of capturing the Linux framebuffer (`/dev/fb0` and overlay `/dev/fb1`) and encoding PNG screenshots to `/tmp/screenshot.png` (and USB/SD diagnostic dumps) on VMF command. Shares `VmfThread.cpp` IPC plumbing with `opt/hmi`.

## Recovered Source Files

- `service/vmf/VmfThread.cpp`

## Key Functions

- [`0xb0fc`](functions/0xb0fc.c) — `main`: creates `QCoreApplication`, starts the VMF listener thread, and enters `QCoreApplication::exec()`.
- [`0xc8a8`](functions/0xc8a8.c), [`0xe324`](functions/0xe324.c), [`0x1067c`](functions/0x1067c.c), [`0x11944`](functions/0x11944.c) — Framebuffer capture (`/dev/fb0`/`/dev/fb1`) and PNG encoding routines.
