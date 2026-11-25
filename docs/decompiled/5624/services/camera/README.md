[← Decompiled Firmware Index](../../README.md)

# `opt/camera` — Early-Boot Reverse Camera (RVC) Daemon (`5624_vw`)

- **Target binary**: `opt/camera` (`14,428` bytes, ARMv7-A Linux ELF, SHA-256 `b2059f0729934674de897c5404b51d910c3108414901d2ca785456acdbc02543`)
- **Decompiled**: `79` written / `110` total / `31` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md) · [coding.md](../../../../coding.md)

## Overview

Lightweight standalone V4L2 helper launched in the background very early by `/etc/rc.d/rcS` — **before** `opt/vmf`, `opt/pwr_agent`, or `opt/hmi` are running — so Rear View Camera (RVC) video from the `ADV7182` CVBS decoder can appear immediately if the driver shifts into reverse during cold boot:

1. Reads `[CODING] CODINGS` directly from `/pss1/stdplus.ini` (`ini_getl`) and immediately exits if bit 1 (`CODINGS >> 1 == 0`, RVC not coded) is clear.
2. Monitors reverse-gear / handover GPIO `gpio44` (`/sys/class/gpio/gpio44/value`).
3. Opens V4L2 capture (`/dev/video0`), V4L2 IPU overlay output (`/dev/video17`), and `/dev/fb0`, streams live camera frames until `opt/hmi` (`VCameraTask`) takes over or reverse disengages, then unmaps buffers and exits cleanly.

## Key Functions

- [`0x8adc`](functions/0x8adc.c) — `main`: checks `/pss1/stdplus.ini` RVC coding bit, opens `/dev/video0`, `/dev/video17`, and `/dev/fb0`, runs the early-boot capture/overlay loop, and tears down cleanly.
- [`0x9104`](functions/0x9104.c) / [`0x951c`](functions/0x951c.c) — V4L2 capture (`/dev/video0`) and overlay output (`/dev/video17`) setup.
- [`0x992c`](functions/0x992c.c), [`0x9bdc`](functions/0x9bdc.c), [`0x9e18`](functions/0x9e18.c) — CLI/GPIO initialization, reverse-signal polling, and frame streaming loop.
