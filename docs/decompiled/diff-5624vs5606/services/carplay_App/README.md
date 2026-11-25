[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/carplay_App/README.md)

# `opt/carplay_App` — CarPlay Session & UI Coordinator (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/carplay_App` (`114,084` B vs. `5624` `116,508` B, Δ `-2,424` B, ARMv7-A ELF, SHA-256 `eff44c2672be6ddc282f42a24db3da168228399ce9cde365fcde1959d4dcf39d`)
- **Decompiled**: `227` written / `304` total / `77` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Uses `CarPlayInputKnob` instead of `5624`'s `CarPlayInputGeneric`.
- Lacks `5624`'s foreground/background timeout (`CPA_StartFBTimer`/`CPA_StopFBTimer`), `CPA_EVENT_TIMEOUT_WAIT_SOURCE_OFF`, and RVC interaction handling (`b_isMusicStartInRVC`, phone call / music start while RVC active).
