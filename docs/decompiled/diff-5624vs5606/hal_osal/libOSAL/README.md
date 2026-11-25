[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/hal_osal/libOSAL/README.md)

# `opt/lib/libOSAL.so.1.0.0` — OS Abstraction & Trace Logging Library (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/lib/libOSAL.so.1.0.0` (`55,656` B vs. `5624` `55,656` B, Δ `+0` B, ARMv7-A ELF, SHA-256 `0194d47182987dfeca2891dd8ee05995c70f9deb6b8e81f75f5c7ee26c131a2d`)
- **Decompiled**: `396` written / `523` total / `127` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- `pa_app_mem_info` ([`0x19574`](functions/0x19574.c)): writes `free -m | awk '{print $2,$3}' > mem` (relative path in read-only `/opt`) vs. `5624` fixing it to `/tmp/mem`.
- Minor trace-filter check adjustments in `printlog` ([`0x1aaec`](functions/0x1aaec.c)).
