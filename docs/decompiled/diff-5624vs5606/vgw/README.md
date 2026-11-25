[← 5606 Diff Index](../README.md) · [5624 Baseline](../../5624/vgw/README.md)

# `opt/vgw` — SPI IPCL ↔ VMF Gateway Daemon (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/vgw` (`23,594` B vs. `5624` `23,594` B, Δ `+0` B, ARMv7-A ELF, SHA-256 `31f87fc8f84cc5e33e4041452d840707873048cdc3b072d813c99fa058a22779`)
- **Decompiled**: `117` written / `146` total / `29` external / `0` failed in [pseudocode/functions/](pseudocode/functions) ([pseudocode/decompile-index.json](pseudocode/decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- **Zero behavioral difference**: `.text` and `.rodata` are 100% byte-identical to `5624_vw`.
- Only 21 bytes in `.symtab`/`.strtab` differ (GCC internal static-variable suffixes such as `__FUNCTION__.6484` vs. `__FUNCTION__.6486`).
