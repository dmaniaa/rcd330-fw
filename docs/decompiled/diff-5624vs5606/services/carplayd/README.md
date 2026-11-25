[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/carplayd/README.md)

# `opt/carplayd` — CarPlay Core IPC Daemon (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/carplayd` (`84,820` B vs. `5624` `83,340` B, Δ `+1,480` B, ARMv7-A ELF, SHA-256 `203a9a56c2c30515f6de4f5fdf8bc33742cfe4b802450c7bfb0e990054a030e7`)
- **Decompiled**: `398` written / `550` total / `152` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Built from Apple CarPlay SDK **`carplay_std_R5_CD30_Sept30`** (`CP_INTERFACE_2.0_1.0_210.81_R6-65-g99ea`) vs. `5624`'s `CD32_Nov11` (`R6-69-gb24f`); minor diff in `CarPlayHandleServerCopyProperty` and `_CarPlayHandleModesChanged`.
