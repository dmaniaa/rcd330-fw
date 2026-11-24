[← Decompiled Firmware Index](../../README.md)

# `opt/carlife_daemon` — Baidu CarLife Projection Daemon (`5624_vw`)

- **Target binary**: `opt/carlife_daemon` (`285,928` bytes, ARMv7-A Linux ELF, SHA-256 `375a0e027946eca3e2b8af1c19f676fea320ac03594e50bbc07b45db608c11a0`)
- **Decompiled**: `1,099` written / `1,296` total / `197` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../architecture.md) · [ipc-and-vgw.md](../../../ipc-and-vgw.md) · [coding.md](../../../coding.md)

## Overview

Baidu CarLife smartphone projection daemon launched by `opt/start_carlife.sh` when Feature Manager `CARLIFE_ATTR` (index `3`) is enabled (disabled via `INVALID` in `/opt/feature_config.ini` on `5624_vw`; enabled on `5606_skoda`). Uses protobuf messaging (`carlife.pb.cc`), GStreamer video decoding (`video_gst_api.cpp`), ALSA audio pipelines for music/TTS/VR (`media_alsa_api.cpp`, `tts_alsa_api.cpp`, `vr_alsa_api.cpp`), USB/EAP transport (`/dev/eap0`, `/opt/md_adapter`), and VMF groups `0x83` / `0xC7` to coordinate display, touch, and audio focus with `opt/hmi` (`VCarLifeProxy`) and `opt/audio_proc`.

## Recovered Source Files

- `main.cpp`, `vmf/src/vmf_adapter.cpp`, `core/CTranRecvPackageProcess.cpp`
- `modules/CConnectionSetupModule.cpp`, `utility/CConnectManager.cpp`, `carlife_msg/carlife.pb.cc`
- `Video/src/video_gst_api.cpp`
- `Audio/src/`: `alsa_generic.cpp`, `media_alsa_api.cpp`, `tts_alsa_api.cpp`, `vr_alsa_api.cpp`
- `Watchdog/src/watchdog_timer.cpp`

## Key Functions

- [`0xc124`](functions/0xc124.c) — `main`: initializes connection manager, VMF adapter, and media/video pipelines.
- [`0x10068`](functions/0x10068.c) (`update_carlife_daemon_state`), [`0x1082c`](functions/0x1082c.c) (`cmdBackground`), [`0x108d0`](functions/0x108d0.c) (`cmdForeground`), [`0x10968`](functions/0x10968.c) (`cmdProtocolVersionMatchStatus`).
- [`0x10d28`](functions/0x10d28.c) (`update_video_state`), [`0x112b0`](functions/0x112b0.c) (`cmdVideoEncoderInitDone`), [`0x11494`](functions/0x11494.c) (`gststateCallback` / `carlife_notify_video_start`).
- [`0x115d8`](functions/0x115d8.c) (`update_music_state`), [`0x11d3c`](functions/0x11d3c.c) (`update_audio_focus_state`), [`0x121bc`](functions/0x121bc.c) (`update_vr_state`), [`0x124b4`](functions/0x124b4.c) (`update_navi_state`), [`0x127c8`](functions/0x127c8.c) (`ttsStop`).
