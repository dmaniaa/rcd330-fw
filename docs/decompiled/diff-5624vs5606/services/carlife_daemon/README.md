[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/carlife_daemon/README.md)

# `opt/carlife_daemon` — Baidu CarLife Daemon (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/carlife_daemon` (`285,924` B vs. `5624` `285,928` B, Δ `-4` B, ARMv7-A ELF, SHA-256 `20d5bd7fe693ce1bcf2799e399a8672ac0a610f3c65b5e2880ddaca21017a0cc`)
- **Decompiled**: `1,098` written / `1,295` total / `197` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Lacks `5624`'s `mutex_video_state` mutex protection around video state transitions (`+68` B `.text` delta).
