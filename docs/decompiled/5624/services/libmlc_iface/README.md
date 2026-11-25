[← Decompiled Firmware Index](../../README.md)

# `opt/lib/mirrorlink/libmlc_iface.so` — MirrorLink Client Stack Library (`5624_vw`)

- **Target binary**: `opt/lib/mirrorlink/libmlc_iface.so` (`1,266,543` bytes, ARMv7-A shared library, SHA-256 `3512fb88894d07d5474daf589f7d70fa94f80c3816f0d4b061efc80709742b02`)
- **Decompiled**: `1,994` written / `2,233` total / `239` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md) · [ipc-and-vgw.md](../../../../ipc-and-vgw.md)

## Overview

Full MirrorLink client stack shared library linked directly into `opt/hmi` (`VMirrorLinkProxy`, VMF groups `0xA4`, `0xC4`, `0xD2`, gated by Feature Manager `MLINK_ATTR`, index `4`). Implements the complete MirrorLink client stack with 761 exported symbols across:
- **Device & Network Discovery**: UPnP/CDC-NCM interface management (`mlciface_nwiface_*`, `mlc_devmgr_*`, `/tmp/mirrorlink_notify_upnp`, `/tmp/mirrorlink_client_notify`, `/tmp/device_path`).
- **Display / VNC Framebuffer Projection**: `dispmgr_*` rendering onto `/dev/fb0` and touch/key input forwarding.
- **Data Services & DAP Certification**: `mlc_dataservmgr_*`, `dapc_read_write_cert`, OpenSSL RSA/SHA-256/HMAC/X.509 certificate validation (`ales_osal_crypto_*`) for Drive-Mode / App Certification checks.
- **OSAL & Logging Abstraction (`ales_osal_*`)**: Self-contained threading, events, crypto, file I/O, and logging helpers.

## Key Exported Functions

- [`0x22328`](functions/0x22328.c) — `dapc_read_write_cert`: Device Attestation Protocol (DAP) certificate storage.
- [`0x230e0`](functions/0x230e0.c)..[`0x23b00`](functions/0x23b00.c) — `ales_osal_crypto_*` (HMAC-SHA256, X.509 certificate date & extended key usage verification, RSA public encrypt, SHA hashing).
- [`0x1fb74`](functions/0x1fb74.c) (`mlc_dataservmgr_register_service_params`), [`0x1fb80`](functions/0x1fb80.c) (`dispmgr_disphandle_list_start_node_msghandler_thread`), [`0x1fbc8`](functions/0x1fbc8.c) (`mlciface_nwiface_list_get_node_count`), [`0x1fbd4`](functions/0x1fbd4.c) (`mlc_devmgr_get_mls_version`).
- [`0x22580`](functions/0x22580.c) / [`0x22ac0`](functions/0x22ac0.c) — `ales_print_log`, `ales_set_log_preference`.
