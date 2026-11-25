[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/carlife_daemon/README.md)

# `opt/carlife_daemon` — Baidu CarLife Daemon (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/carlife_daemon` (`286,236` B vs. `5624` `285,928` B, `.text +344` B, ARMv7-A ELF)
- **Decompiled**: `1,100` written / `1,298` total / `198` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#8-optcarlife_daemon-344-b-text)

## Behavioral Differences vs. `5624_vw`

- `start_media_playback` ([`0x38cfc`](functions/0x38cfc.c), `Audio/src/media_alsa_api.cpp`) drains residual entries from the `std::deque` music buffer on playback stop (`"Music audio buffer is not empty. Empty the buffer!!"`).
- `CTranRecvPackageProcess::packageHeadAnalysis` ([`0x156b0`](functions/0x156b0.c)) resets `param_1[7] = 0` on unknown MD message types and logs the type in hex (`%x`) instead of `%d`.
