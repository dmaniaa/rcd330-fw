[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/AAP_Application/README.md)

# `opt/AndroidAuto/bin/AAP_Application` — Android Auto HMI Coordinator (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/AndroidAuto/bin/AAP_Application` (`154,648` B vs. `5624` `154,596` B, `.text +52` B, ARMv7-A ELF)
- **Decompiled**: `198` written / `264` total / `66` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#11-optandroidautobinaap_application-52-b-text--12-optandroidautobinaap_service-256-b-text)

## Behavioral Differences vs. `5624_vw`

- Single functional difference in `aap_session_status_cb` ([`0xcce0`](functions/0xcce0.c) vs. `5624 0xcca8`): RCD 780 only checks `if (local_b0 == 0x10f)`, lacking `5624`'s `|| local_b0 == 0x113` framing-error disconnect check (paired with `opt/AndroidAuto/bin/aap_service`).
