[← Decompiled Firmware Index](../../README.md)

# `opt/mfid` — Apple MFi Authentication Coprocessor Daemon (`5624_vw`)

- **Target binary**: `opt/mfid` (`31,360` bytes, ARMv7-A Linux ELF, SHA-256 `97718cbefe5913cb52c582c8667500a12a44d906fea9cd41ec05196d3189f44f`)
- **Decompiled**: `252` written / `329` total / `77` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Related**: [iap2d/README.md](../iap2d/README.md) · [carplayd/README.md](../carplayd/README.md) · [architecture.md](../../../architecture.md)

## Overview

Apple MFi hardware authentication daemon started directly by `/opt/launch.sh`. Accepts CLI flags for the Apple Auth Coprocessor transport type (`i2c` / `dln`), I2C device path and address, and reset GPIO (`/sys/class/gpio/...`), reads/caches the coprocessor certificate, and exposes a protobuf RPC server (`MFIIPCServer.c` / `rpcmessage.pb-c.c`) used by `opt/iap2d` and `opt/carplayd` to sign challenge-response authentication exchanges with connected iPhones.

## Recovered Source Files

- `MFIIPCServer.c`, `MFIInterface.c`
- `IAP2/rpc/common/c_source/src/rpcmessage.pb-c.c`

## Key Functions

- [`0x95e4`](functions/0x95e4.c) — `main`: daemonizes, parses device type / I2C path / I2C address / reset GPIO CLI args, initializes the MFi coprocessor ([`0xa584`](functions/0xa584.c)), creates the RPC server ([`0xb0ec`](functions/0xb0ec.c) with handler [`0xa2c0`](functions/0xa2c0.c)), and prefetches the certificate ([`0xa69c`](functions/0xa69c.c)).
- [`0x9d9c`](functions/0x9d9c.c), [`0xa998`](functions/0xa998.c), [`0xacd8`](functions/0xacd8.c) — MFi coprocessor I2C protocol and RPC serialization routines.
