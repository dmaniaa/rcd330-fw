[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/sysupdate/README.md)

# `opt/sysupdate` — Software Update & Flash Manager (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/sysupdate` (`716,016` B vs. `5624` `724,208` B, Δ `-8,192` B, ARMv7-A ELF, SHA-256 `aff46843ee874f904a37e5429fcda60ceb9c8ef0559a8ca8bc929713ad4f8d58`)
- **Decompiled**: `2,279` written / `2,649` total / `370` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Core unsigned `.tar` update flow matches `5624` (`0` non-asset logic string differences).
- Standalone SWDL GUI embeds `5606`'s Škoda green asset list (`45` files in `opt/data/sysupdate_source/` vs. `82` in `5624`, including `1btn_bg_*_{disabled,pressed}_ovg.bin` instead of `5624`'s dual `_0`/`_1` frames and glow borders).
