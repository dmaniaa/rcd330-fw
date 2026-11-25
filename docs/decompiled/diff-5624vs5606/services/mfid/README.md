[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/mfid/README.md)

# `opt/mfid` — Apple MFi Auth Coprocessor Daemon (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/mfid` (`28,816` B vs. `5624` `31,360` B, Δ `-2,544` B, ARMv7-A ELF, SHA-256 `5d13c205455b8820867f48904483ba44fbc38851b07edb8df6f2673df5fdd9de`)
- **Decompiled**: `247` written / `322` total / `75` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Older **`$IAP: 5.2 $` / `CP_IAP2_R3`** (`−664` B `.text`).
- Lacks `5624`'s configurable I2C address / device-path / reset-GPIO option parsing (`addr=%s`, `Device I2C address: 0x%x`); retains verbose `E_GETDEVICEID`/`E_GETCERTIFICATE`/`E_CREATESIGNATURE` enter/exit trace logs.
