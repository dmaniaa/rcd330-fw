[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/media/README.md)

# `opt/media` — Media Playback & iPod Service (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/media` (`382,284` B vs. `5624` `384,008` B, Δ `-1,724` B, ARMv7-A ELF, SHA-256 `31d7c9f3288d9c2576e21cf406a4d2c069939b6ed6d3af23ec5d02f3c247fe5c`)
- **Decompiled**: `1,152` written / `1,299` total / `147` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Lacks `5624`'s explicit iPod host-mode file-descriptor `close()` error check (`ERROR: host mode close ipod fd is failed! `).
