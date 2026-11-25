[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/exlap/README.md)

# `opt/exlap` — VW EXLAP Vehicle Data Server (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/exlap` (`751,740` B vs. `5624` `751,740` B, exact 1-byte diff, ARMv7-A ELF)
- **Decompiled**: `1,977` written / `2,212` total / `235` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#10-optexlap-0-b-delta--exact-1-byte-diff)

## Behavioral Differences vs. `5624_vw`

- **Exact 1-byte diff across the entire `751,740`-byte binary**: at `VA 0x1c0b4` inside `exlap_cardata_hmi_msg_handler` ([`0x1bff4`](functions/0x1bff4.c)), `cmp r12, #0x90` (`uVar3 < 0x91`) in RCD 780 vs. `cmp r12, #0x91` (`uVar3 < 0x92`) in `5624`.
