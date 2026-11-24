[← Decompiled Firmware Index](../../README.md)

# `opt/lib/libCarPlayPlatformSystem.so` — CarPlay Platform Thread & Watchdog Library (`5624_vw`)

- **Target binary**: `opt/lib/libCarPlayPlatformSystem.so` (`7,096` bytes, ARMv7-A shared library, SHA-256 `75c983c2ede364c3417add958951b2d68bd44e0c54b3149f86b3fd7f1bbf6bf9`)
- **Decompiled**: `71` written / `97` total / `26` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Related**: [carplayd/README.md](../carplayd/README.md) · [libCarPlayAudio/README.md](../libCarPlayAudio/README.md)

## Overview

Small platform-support shared library linked by `opt/carplayd` and `libCarPlayAudio.so`. Provides Linux thread registration, heartbeat watchdog monitoring (`CarPlaySystemHeartbeatThread`), and lightweight VMF helper wrappers for the CarPlay runtime.

## Recovered Source Files

- `YFVE-STDPlus/SystemPlatform/src/CarPlayPlatformSystemLinux.c`

## Key Exported Functions

- **System & Heartbeat**: [`0x10ee0`](functions/0x10ee0.c) (`CarPlaySystemInit`), [`0x10fb4`](functions/0x10fb4.c) (`CarPlaySystemRegisterThread`), [`0x1106c`](functions/0x1106c.c) (`CarPlaySystemUnregisterThread`), [`0x1109c`](functions/0x1109c.c) (`CarPlaySystemHeartbeatThread`).
- **VMF Helpers**: [`0x111c8`](functions/0x111c8.c) (`CarPlayVMFCreate`), [`0x1124c`](functions/0x1124c.c) (`CarPlayVMFStart`), [`0x112b4`](functions/0x112b4.c) (`CarPlayVMFSendMsg`), [`0x1133c`](functions/0x1133c.c) (`CarPlayVMFFinalize`).
