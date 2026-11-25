[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/pwr_agent/README.md)

# `opt/pwr_agent` — Power Manager & Process Supervisor (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/pwr_agent` (`48,824` B vs. `5624` `49,316` B, Δ `-492` B, ARMv7-A ELF, SHA-256 `21f2818f7934b5ea513bfe903996f6447d8fd7c14414becf190817adf30543c5`)
- **Decompiled**: `227` written / `303` total / `76` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Lacks `5624`'s `pa_app_read_trace_configuration` (`/pss1/config/pwr_agent.conf`, `SAVESD_ENABLE`) dynamic trace config loader.
- Power mode log omits ACC state (`Receive mode: %s, reason %d.` vs. `5624`'s `Receive mode: %s, reason %d, acc state %d.`).
