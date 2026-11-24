[← Decompiled Firmware Index](../../README.md)

# `opt/pss` — Persistent Storage Service & Feature Manager (`5624_vw`)

- **Target binary**: `opt/pss` (`53,964` bytes, ARMv7-A Linux ELF, SHA-256 `b395e3d290fedad810b095a85be331487132de6dd97c8c0186e441e565d64945`)
- **Decompiled**: `140` written / `183` total / `43` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [coding.md](../../../coding.md) · [ipc-and-vgw.md](../../../ipc-and-vgw.md)

## Overview

Manages all non-volatile `.pss` binary blocks across the mirrored `/pss1` (`mtd3`) and `/pss2` (`mtd4`) UBIFS partitions, plus the **Feature Manager** flag table:
- **VMF Interface**: Serves read/write requests on `VMF 0x8C` (responses on `0x72`, plus client-specific reply groups) and handles VIP PSS requests.
- **PSS File Format**: Every `.pss` block starts with an 8-byte Big-Endian header (`CRC-16/CCITT-FALSE` over payload using table at `VA 0x10ac0`, `uint16_t data_len`, `uint8_t tag`, `uint16_t write_counter`, `uint8_t reserved`) followed by `data_len` raw payload bytes (e.g., Block `0x50` `DIAG_PSS_CODING.pss` and Block `0x54` `FEATURE_MANAGER_PSS_BLK.pss`, both 25-byte payloads / 33 bytes on disk).
- **Feature Manager**: Combines `FEATURE_MANAGER_PSS_BLK.pss` (11 feature flags `0..10`: BT, RVC, CarPlay, CarLife, MirrorLink, NewBAP, Exlap, ETM, OPS, Climate, Android Auto) with static overrides in `/opt/feature_config.ini` (`TRUE` = forced ON, `INVALID` = forced OFF, `FALSE` = dynamic from PSS) and pushes the resolved 11-byte vector to `opt/pwr_agent` (`VMF 0x0B:0x06`).

## Recovered Source Files

- `src/pss_main.c`, `src/pss_nvm_manager.c`, `src/pss_file_access.c`
- `src/pss_feature_manager.c`, `src/pss_wake_manager.c`
- `src/pss_vmf_comm.c`, `src/pss_vmf_receive.c`, `src/pss_vmf_send.c`

## Key Functions

- [`0x8e90`](functions/0x8e90.c) — `main` / `pss_evaluate_internal_message` / `handle_pss_timeout_event`.
- [`0x9510`](functions/0x9510.c) — `pss_vmf_comm_thread` / `handle_recvd_vmf_msg`.
- [`0x9a78`](functions/0x9a78.c) — `pss_evaluate_vmf_PSS_IN`: dispatches `handle_pss_read_req`, `handle_pss_write_req`, `handle_feature_manager_read_req`, and shadow debug dump helpers.
- [`0x9c30`](functions/0x9c30.c) — `pss_evaluate_vmf_PSS_VIP_OUT`: handles VIP-side PSS read/write requests.
- [`0x9e18`](functions/0x9e18.c) — MQB `DIAG_OUT` sub `0x16` adaptation hook updating Feature Manager bits.
- [`0xa848`](functions/0xa848.c), [`0xb258`](functions/0xb258.c) — Dual-partition `.pss` read/write and CRC-16 validation routines.
- [`0xdb34`](functions/0xdb34.c), [`0xe0c0`](functions/0xe0c0.c), [`0xe44c`](functions/0xe44c.c), [`0xf068`](functions/0xf068.c) — Feature Manager storage, `/opt/feature_config.ini` override resolution, and broadcast (`VMF 0x0B:0x06`).
