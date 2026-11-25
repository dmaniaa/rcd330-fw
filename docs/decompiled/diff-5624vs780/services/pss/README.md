[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/pss/README.md)

# `opt/pss` — Persistent Storage Service (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/pss` (`54,156` B vs. `5624` `53,964` B, `.text +0` B, `.rodata +192` B, ARMv7-A ELF)
- **Decompiled**: `140` written / `183` total / `43` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#3-optpss-0-b-text-192-b-rodata)

## Behavioral Differences vs. `5624_vw`

- Identical executable `.text`; expands the static PSS block descriptor table from `100` (`0x64`) to **`102` (`0x66`)** dual-partition (`type=2`) blocks to back `opt/diag_eol`:
  - Block `100` (`0x64`): `V_EOL_PSS_SECURITY_CODE_PASSWORD.pss` (16 B on disk: 8 B header + 8 B payload).
  - Block `101` (`0x65`): `V_EOL_PSS_PUBLIC_KEY_BLK.pss` (40 B on disk: 8 B header + 32 B HAB SRK hash).
