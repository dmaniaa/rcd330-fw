[← Decompiled Firmware Index](../../README.md)

# `opt/AndroidAuto/bin/AAP_Application` — Android Auto HMI/VMF Projection App (`5624_vw`)

- **Target binary**: `opt/AndroidAuto/bin/AAP_Application` (`154,596` bytes, ARMv7-A Linux ELF, SHA-256 `dc80403b0426f8f88e1042f00e5f078ece8306dfecf95ea49934662d22470a1e`)
- **Decompiled**: `198` written / `264` total / `66` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../architecture.md) · [ipc-and-vgw.md](../../../ipc-and-vgw.md) · [Android Auto ECNR Echo Bug](../../../bugs/android-auto-echo-ecnr.md)

## Overview

Android Auto headunit application launched by `opt/launch_aa.sh` alongside `aap_service` (gated by Feature Manager `ANDAUTO_ATTR`, index `10`). Reads headunit capabilities and audio device mapping (including 16 kHz mono mic capture on ALSA `hw:0,4`) from `/etc/aap_system_attributes.xml`, drives an 8-state (`0..7`) projection state machine (`AAA_StateMachine`), and bridges Google's AAP SDK callbacks to `opt/hmi` (`VAndroidAutoProxy`) over VMF groups **`0xDC`** (AA → HMI) and **`0xDD`** (HMI → AA), as well as `opt/pwr_agent` and `opt/audio_proc`.

## Recovered Source Files

- `../src/AAA_Dispatcher.c`, `../src/AAA_Vmf.c`, `../src/AAA_StateMachine.c`
- `../src/AAA_SMTimer.c`, `../src/AAA_Queue.c`, `../src/AAA_Timer.c`, `../src/AAA_HealthMonitor.c`

## Key Functions

- [`0x9820`](functions/0x9820.c) — `main`: initializes state machine, VMF threads, and AAP session callbacks.
- [`0xd8b4`](functions/0xd8b4.c) — `AAA_CMDFromSM`: state-machine command executor (`case 0x13` sends `AA_HMI_REQ_AUDIO_SETUP` and media mute/unmute requests for `0x14` music / `0x15` VR).
- [`0x12700`](functions/0x12700.c) / [`0x12320`](functions/0x12320.c) — `HandleVmfMsgFromHMI` (touch/key forwarding, resource availability flags `[0xc4]`/`[0xc5]`, focus responses) & `HandleVmfMsgFromPA`.
- [`0x12f18`](functions/0x12f18.c), [`0x13328`](functions/0x13328.c), [`0x135f0`](functions/0x135f0.c) — `nProcessEvents`, `AAA_StateMachineInit`, `AAA_StateMachineSendEvent` (state transition table at `PTR_DAT_000207d8`).
- **AAP SDK Callbacks**: [`0xbd08`](functions/0xbd08.c) (`aap_audio_state_cb` — handles `0xa02` Guidance, `0xa03` Media, `0xa04` System, `0xa06` VR mic, including the standby-mode VR/Guidance STOP bypass), [`0xc348`](functions/0xc348.c) (`aap_stream_request_cb`), [`0xc958`](functions/0xc958.c) (`aap_projection_status_cb`), [`0xcca8`](functions/0xcca8.c) (`aap_session_status_cb`), [`0xaecc`](functions/0xaecc.c) (`aap_phone_status_cb`), [`0xb140`](functions/0xb140.c) (`aap_nav_status_cb`), [`0xb620`](functions/0xb620.c) (`aap_media_playback_cb`), [`0x12cac`](functions/0x12cac.c) (`aap_bt_pairing_req_cb`).
