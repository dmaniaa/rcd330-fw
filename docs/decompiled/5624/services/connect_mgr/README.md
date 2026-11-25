[← Decompiled Firmware Index](../../README.md)

# `opt/connect_mgr` — USB Device Detection & Projection Session Coordinator (`5624_vw`)

- **Target binary**: `opt/connect_mgr` (`55,560` bytes, ARMv7-A Linux ELF, SHA-256 `4395e72e96d6a8d6ee9eba94d4f2f1e2ffd49b4222606067f964d4503b2dc804`)
- **Decompiled**: `233` written / `308` total / `75` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md) · [ipc-and-vgw.md](../../../../ipc-and-vgw.md) · [coding.md](../../../../coding.md)

## Overview

USB smartphone connection manager spawned by `opt/pwr_agent`. Monitors USB hotplug uevents on the i.MX6 EHCI port (`/sys/devices/platform/fsl-ehci.0/usb1/1-1/...`, `/dev/ipodout0`, `/tmp/devlist.txt`), queries Feature Manager flags (`VMF 0x8C:0x08`) to determine which projection protocols are enabled, identifies attached phones, triggers USB role switching for Apple CarPlay (`/proc/driver/usbroleswitch/0/mode`), negotiates Android Open Accessory Protocol (AOAP) for Android Auto, activates CDC-NCM for MirrorLink, and coordinates active sessions over VMF groups `0x8D`, `0xC8`, and `0xC9` with `opt/hmi` (`VAppConnectProxy`, `VCarPlayProxy`, `VAndroidAutoProxy`, `VCarLifeProxy`, `VMirrorLinkProxy`).

## Recovered Source Files

- `src/main.c`
- `src/device_detection.c`, `src/device_identify.c`
- `src/cm_com_vmf.c`

## Key Functions

- [`0x9790`](functions/0x9790.c) — `main`: initializes timers, VMF communication, and device detection thread.
- [`0xa658`](functions/0xa658.c) — `cm_create_device_detect_thread`: USB uevent monitor.
- [`0xa030`](functions/0xa030.c) — `cm_evaluate_internal_message` / `ios_device_identify` / `ios_role_switch_logic_handle`.
- [`0x9670`](functions/0x9670.c), [`0x10aa8`](functions/0x10aa8.c), [`0x116fc`](functions/0x116fc.c), [`0x117b8`](functions/0x117b8.c) — `cm_carplay_support_check`, `iAP1_device_check`, `cm_ios_roleswitch_timeout_handle`, `ios_start_role_switch_handle`.
- [`0xa0f8`](functions/0xa0f8.c)..[`0xa44c`](functions/0xa44c.c) — Role-switch, MirrorLink NCM, AOAP accessory, and uevent timers.
- [`0xc9a0`](functions/0xc9a0.c) — Main VMF message handler (including Feature Manager query `VMF 0x8C:0x08`).
