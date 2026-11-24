[← Decompiled Firmware Index](../../README.md)

# `opt/audio_proc` — Audio Manager & Connection Master (`5624_vw`)

- **Target binary**: `opt/audio_proc` (`168,636` bytes, ARMv7-A Linux ELF, SHA-256 `8d94e46c43806d68d5372fd5c8b6d6c3d429194eaa992b36a1b54fe0b93d872e`)
- **Decompiled**: `693` written / `833` total / `140` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [ipc-and-vgw.md](../../../ipc-and-vgw.md) · [architecture.md](../../../architecture.md) · [Android Auto ECNR Echo Bug](../../../bugs/android-auto-echo-ecnr.md)

## Overview

Central Linux Audio Manager (`audio_master`) and Connection Master (`connection_master`) daemon coordinating volume, mute, EQ/fader/balance, audio focus arbitration, MirrorLink ALSA playback, and SAF775x DSP control over the VIP MCU:
- **VMF Interfaces**:
  - Receives audio requests from `opt/hmi` (`VAudioProxy`, `VMirrorLinkProxy`) and projection services on groups `0x3C`, `0x3E`, `0x47`.
  - Publishes primary volume (`0..30`, entertainment/phone/ring/SDS), mute state, and channel grants on `0x3D`, `0x3F`, `0x45` (e.g., `0x3F:0x01` `AUDIO_AM_PRIMARY_VOLUME_STATUS` & `AUDIO_AM_HMI_MUTE_STATUS`).
  - Commands the RH850 VIP / SAF775x DSP via `VMF 0x8F:0x1F` → `IPCL(0x03, 0x00)` (source connect, mute, volume, EQ/dataset, PSS read/write) and receives ACKs on `VMF 0x50:0x02` ← `IPCL(0x03, 0x02)`.
- **Local Audio / Test Assets**: Plays local WAV ringtones (`/opt/ringtone/5.wav`) and EOL test tones (`/opt/eol_test.wav`), and manages MirrorLink VAF audio (`/tmp/vaf_comm_mirrorlink`, `ml_alsa.c`).

## Recovered Source Files

- `core/audio_master/src/`: `am_main.c`, `am_wake_state.c`, `audio_asink.c`, `audio_channel.c`, `audio_stream.c`, `audio_vmf.c`
- `core/connection_master/src/`: `cm_main.c`, `cm_connection_handlers.c`, `cm_event_handlers.c`, `cm_vmf_msg_receive.c`
- `core/mlink_alsa_player/src/ml_alsa.c`
- `app/audio_vaf_comm/src/audio_vaf_mlink_task.c`

## Key Functions

- [`0xa894`](functions/0xa894.c) — `main`: initializes OSAL, Audio Master, and Connection Master tasks.
- [`0x10074`](functions/0x10074.c), [`0x108ac`](functions/0x108ac.c), [`0x12e7c`](functions/0x12e7c.c), [`0x12f84`](functions/0x12f84.c) — `audio_channel_set_source_db`, `audio_channel_get_audio_setting_all`, `audio_channel_fast_mute`, `audio_channel_wait_fast_mute`.
- [`0x12284`](functions/0x12284.c), [`0x1245c`](functions/0x1245c.c), [`0x1256c`](functions/0x1256c.c) — Speaker, mic, and MMI diagnostic/EOL test modes.
- [`0x13270`](functions/0x13270.c), [`0x136dc`](functions/0x136dc.c), [`0x13c04`](functions/0x13c04.c) — `audio_calculate_coefficients_task` / `audio_cal_dataset`, `audio_recv_dsp_eq_resp`, `audio_update_alternate_volume`.
- [`0x1b274`](functions/0x1b274.c), [`0x1c124`](functions/0x1c124.c), [`0x1cc68`](functions/0x1cc68.c), [`0x1d2dc`](functions/0x1d2dc.c) — Audio source routing & VIP DSP command handlers (including AA Media `0x14` / AA VR `0x15` / BT Phone `0x09`).
