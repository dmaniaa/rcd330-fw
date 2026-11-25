[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/audio_proc/README.md)

# `opt/audio_proc` — Audio Manager & ALSA/DSP Router (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/audio_proc` (`168,404` B vs. `5624` `168,636` B, Δ `-232` B, ARMv7-A ELF, SHA-256 `5e684d223d3d6b69e97210b37d499af0ce2cc37f968161f9f83a0f88e721f449`)
- **Decompiled**: `690` written / `830` total / `140` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Uses older 4-arg `[AM==>GRP:%d,EVT:%d]` trace format and SD-specific Android Auto unmute log (`[Audio] send unmute to AA when SD response` vs. `5624`'s generalized `when source %d response`).
