[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/connect_mgr/README.md)

# `opt/connect_mgr` — USB Smartphone Connection Manager (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/connect_mgr` (`53,028` B vs. `5624` `55,560` B, Δ `-2,532` B, ARMv7-A ELF, SHA-256 `c16a156d659d91ce6f3b53be932377f4a93080ee7e77f437f4a53ce85e7eb0ce`)
- **Decompiled**: `230` written / `304` total / `74` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Lacks `5624`'s dynamic feature-flag checks (`FEATURE_AA_ENABLE`, `FEATURE_CL_ENABLE`, `FEATURE_CP_ENABLE`, `FEATURE_ML_ENABLE`) and `subproject` logging.
- Uses older generic USB role-switch error handling (`received role switch command(wValue 0x%02x) response error.`) instead of `5624`'s separate CarPlay/CarLife role-switch routines and `setsockopt` socket setup.
