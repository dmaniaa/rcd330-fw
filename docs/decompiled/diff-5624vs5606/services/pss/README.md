[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/pss/README.md)

# `opt/pss` — Persistent Storage Service (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/pss` (`53,824` B vs. `5624` `53,964` B, Δ `-140` B, ARMv7-A ELF, SHA-256 `757d336a1fbe024acefc045a3b0ca7402dfd0139cdf8e0d76a2096fffbcaf894`)
- **Decompiled**: `140` written / `183` total / `43` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Manages `99` dual-partition PSS blocks (`5624` adds Block `99`: `HMI_CAR_SETUP_SYSTEM_TYPE_PSS_BLK.pss` to reach `100` blocks; RCD 780 reaches `102`).
