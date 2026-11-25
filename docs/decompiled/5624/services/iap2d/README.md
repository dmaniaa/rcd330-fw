[← Decompiled Firmware Index](../../README.md)

# `opt/iap2d` — Apple iAP2 Protocol Daemon (`5624_vw`)

- **Target binary**: `opt/iap2d` (`169,152` bytes, ARMv7-A Linux ELF, SHA-256 `3eff2cfdc63bb7614552c0d0261c8ca88a148b1dae13a0816ede6e9f60c13669`)
- **Decompiled**: `495` written / `576` total / `81` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Related**: [mfid/README.md](../mfid/README.md) · [carplayd/README.md](../carplayd/README.md) · [connect_mgr/README.md](../connect_mgr/README.md)

## Overview

Apple iPod Accessory Protocol 2 (iAP2) daemon launched alongside CarPlay. Exposes a protobuf-based RPC IPC server (`IAP2IPCServer.c` / `rpcmessage.pb-c.c`), authenticates attached iOS devices via `opt/mfid` (and I2C `/dev/i2c-2` / GPIO helpers), and handles iAP2 sessions including Now Playing metadata, Power updates, Call/Communications updates, Vehicle Status updates, External Accessory Protocol (EAP) sessions, and Out-of-Band Bluetooth pairing (`OOBBTPairing`).

## Recovered Source Files

- `IAP2IPCServer.c`, `IAP2Interface.c`
- `IAP2/rpc/common/c_source/src/rpcmessage.pb-c.c`

## Key Functions

- [`0xa28c`](functions/0xa28c.c) — `main`: daemonizes and starts the iAP2 RPC server ([`0x11548`](functions/0x11548.c) with handler [`0xaaa8`](functions/0xaaa8.c)).
- [`0x1e388`](functions/0x1e388.c), [`0xaaa8`](functions/0xaaa8.c) — Core iAP2 state machine and RPC server request handler.
- [`0xd148`](functions/0xd148.c) (`IAP2_NowPlayingUpdates`), [`0xd67c`](functions/0xd67c.c) (`IAP2_PowerUpdates`), [`0xd8cc`](functions/0xd8cc.c) (`IAP2_CallStateUpdate`), [`0xdd44`](functions/0xdd44.c) (`IAP2_CommunicationsUpdate`), [`0xe250`](functions/0xe250.c) (`IAP2_StartVehicleStatusUpdates`).
- [`0xe750`](functions/0xe750.c) (`IAP2_DeviceUUIDNotification`), [`0xec4c`](functions/0xec4c.c)..[`0xf1b0`](functions/0xf1b0.c) (`IAP2_ExternalAccessory*`), [`0xf400`](functions/0xf400.c) / [`0xf408`](functions/0xf408.c) (`IAP2_StartOOBBTPairing`, `IAP2_OOBBTPairingLinkKeyInfo`).
- [`0x118fc`](functions/0x118fc.c)..[`0x11afc`](functions/0x11afc.c) — RPC opcode / argument marshalling (`RPCSetOpcode`, `Marshall_*`).
