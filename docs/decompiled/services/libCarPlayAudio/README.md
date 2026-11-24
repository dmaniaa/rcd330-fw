[← Decompiled Firmware Index](../../README.md)

# `opt/lib/libCarPlayAudio.so` — CarPlay ALSA Audio & VMF Bridge Library (`5624_vw`)

- **Target binary**: `opt/lib/libCarPlayAudio.so` (`20,160` bytes, ARMv7-A shared library, SHA-256 `ace54b29e7659c6baa1858fe001719a4bfe22c28568b9ac0d3d887e10cdcee02`)
- **Decompiled**: `150` written / `212` total / `62` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Related**: [carplayd/README.md](../carplayd/README.md) · [libCarPlayPlatformSystem/README.md](../libCarPlayPlatformSystem/README.md) · [audio_proc/README.md](../audio_proc/README.md)

## Overview

Platform audio plugin library loaded by `opt/carplayd`. Implements CarPlay audio stream setup, ALSA PCM playback/capture (`snd_pcm_*`, using `/etc/asound.conf` devices including `carplay_defcapture` / `hw:0,4`), and VMF notifications to coordinate CarPlay audio state with the headunit's audio manager.

## Recovered Source Files

- `Audio/src/CarPlayPlatformAudioALSA.c`

## Key Exported Functions

- **Audio Stream Lifecycle**: [`0x12694`](functions/0x12694.c) (`CarPlayAudioInit`), [`0x126b8`](functions/0x126b8.c) (`CarPlayAudioGetInfo`), [`0x126d8`](functions/0x126d8.c) (`CarPlayAudioCreate`), [`0x127a4`](functions/0x127a4.c) (`CarPlayAudioPrepare`), [`0x12de4`](functions/0x12de4.c) (`CarPlayAudioStart`), [`0x13340`](functions/0x13340.c) (`CarPlayAudioStop`).
- **VMF Transport Helpers**: [`0x13910`](functions/0x13910.c) (`CarPlayVMFCreate`), [`0x139d4`](functions/0x139d4.c) (`CarPlayVMFStart`), [`0x13a3c`](functions/0x13a3c.c) (`CarPlayVMFSendMsg`), [`0x13ac4`](functions/0x13ac4.c) (`CarPlayVMFFinalize`).
