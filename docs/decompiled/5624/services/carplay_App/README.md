[← Decompiled Firmware Index](../../README.md)

# `opt/carplay_App` — Apple CarPlay HMI/VMF Integration App (`5624_vw`)

- **Target binary**: `opt/carplay_App` (`116,508` bytes, ARMv7-A Linux ELF, SHA-256 `2c7feb949b18b769d097c2641f646fca4d585af5c02fbfb8cb138d20e2b7d8b3`)
- **Decompiled**: `229` written / `306` total / `77` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md) · [ipc-and-vgw.md](../../../../ipc-and-vgw.md)

## Overview

Application-layer Apple CarPlay controller launched by `opt/start_carplay.sh` (gated by Feature Manager `CARPLAY_ATTR`, index `2`, and `BT_ATTR`, index `0`). Bridges the low-level CarPlay protocol daemon (`opt/carplayd`) to the rest of the headunit over VMF:
- **VMF Interfaces**: Communicates with `opt/hmi` (`VCarPlayProxy`) on groups `0x81`, `0xC2`, `0xC3`, with `opt/audio_proc` for media/call/Siri/ducking focus (`HandleVmfMsgFromAudio`), and with `opt/pwr_agent` (`HandleVmfMsgFromPA`).
- **Display & State Machine**: Runs the CarPlay Application state machine (`CPA_StateMachine.c`) managing foreground/background transitions, video overlay (`/dev/fb1`), screen/audio resource requests (`_ResourceRequest`), touch/key forwarding, and health monitoring.

## Recovered Source Files

- `CarPlay_App/src/CPA_Interface.c`
- `CarPlay_App/src/CPA_StateMachine.c`, `CPA_SMTimer.c`, `CPA_Queue.c`, `CPA_Timer.c`
- `CarPlay_App/src/CarPlayVMF.c`

## Key Functions

- [`0x9928`](functions/0x9928.c) — `main`: initializes CarPlay state machine, VMF threads, and `carplayd` IPC client.
- [`0xe8cc`](functions/0xe8cc.c) — Main CarPlay event / callback dispatcher (`carplayd` ↔ state machine).
- [`0x13afc`](functions/0x13afc.c), [`0x1418c`](functions/0x1418c.c), [`0x17c34`](functions/0x17c34.c) — `_ResourceRequest`, `CPA_CheckChannelBarrier`, `CPA_HandleForeBackCmd`.
- [`0x14644`](functions/0x14644.c), [`0x14bb0`](functions/0x14bb0.c) — `HandleVmfMsgFromAudio`, `ThreadGetMsgFromVmf` / `HandleVmfMsgFromHMI` / `HandleVmfMsgFromPA`.
- [`0x18034`](functions/0x18034.c), [`0x18274`](functions/0x18274.c) — `CPA_StateMachineInit`, `CPA_StateMachineSendEvent`.
