[← Decompiled Firmware Index](../../README.md)

# `opt/tools/vmf_rcv` — VMF Packet Monitor CLI Tool (`5624_vw`)

- **Target binary**: `opt/tools/vmf_rcv` (`7,555` bytes, ARMv7-A Linux ELF, SHA-256 `f271915bf25ea805dd519c52535b5d80abbed17137100d90ebc1063fbab64252`)
- **Decompiled**: `37` written / `51` total / `14` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [ipc-and-vgw.md](../../../ipc-and-vgw.md)

## Overview

Stock command-line debugging tool shipped in the retail firmware for subscribing to one or more VMF group IDs and dumping received packets in decimal to stdout.

### Usage
```sh
/opt/tools/vmf_rcv <dec_group1> [dec_group2 ...]
```
Connects as client `"test_receive"` (`nw_vmf_connect`), registers all specified decimal group IDs (`nw_vmf_register_msg_group`), and loops on `nw_vmf_receive`, printing `group id`, `message id`, and decimal payload bytes.

## Key Functions

- [`0x8758`](functions/0x8758.c) — `main`: hands CLI arguments to `vmf_receive_msg`.
- [`0x87bc`](functions/0x87bc.c) — `vmf_receive_msg`: parses group IDs, connects to `opt/vmf`, registers groups, and prints received VMF frames.
