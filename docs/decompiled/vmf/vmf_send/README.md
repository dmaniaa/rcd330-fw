[← Decompiled Firmware Index](../../README.md)

# `opt/tools/vmf_send` — VMF Packet Injection CLI Tool (`5624_vw`)

- **Target binary**: `opt/tools/vmf_send` (`7,038` bytes, ARMv7-A Linux ELF, SHA-256 `24187d06bdc4f317890c2e3b67528c4d9db3ed866a7916e48844a5954076dd5c`)
- **Decompiled**: `30` written / `41` total / `11` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [ipc-and-vgw.md](../../../ipc-and-vgw.md)

## Overview

Stock command-line debugging tool shipped in the retail firmware for injecting arbitrary VMF packets onto the running `opt/vmf` bus. Useful both on hardware and inside QEMU (`-M rcd330`) for exercising HMI/service handlers without modifying binaries.

### Usage
```sh
/opt/tools/vmf_send <dec_group> <dec_sub> [dec_byte0 dec_byte1 ...]
```
Connects as client `"test_send"` (`nw_vmf_connect`), packs a standard 26-byte-header VMF frame (`[0]=0x02`, `[0x14]=group`, `[0x15]=sub`, `*(uint16_t*)(+0x18)=len`, payload at `+0x1A`), transmits via `nw_vmf_send_basic`, and disconnects.

## Key Functions

- [`0x86cc`](functions/0x86cc.c) — `main`: parses decimal CLI arguments, connects to `opt/vmf`, builds and sends the VMF frame, and disconnects.
