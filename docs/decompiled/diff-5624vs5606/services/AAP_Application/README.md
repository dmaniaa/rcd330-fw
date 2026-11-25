[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/AAP_Application/README.md)

# `opt/AndroidAuto/bin/AAP_Application` — Android Auto HMI Coordinator (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/AndroidAuto/bin/AAP_Application` (`147,660` B vs. `5624` `154,596` B, Δ `-6,936` B, ARMv7-A ELF, SHA-256 `7909f0c3bda44afbd00727b4ca3c81383039e30cee274627bd47f9ad61ad0177`)
- **Decompiled**: `175` written / `230` total / `55` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Older **`AAP 5.2.2`** build (`−3,860` B `.text`, `−1,039` B `.rodata`).
- Uses `HMI_AA_NOTIFY_HARDKEY_EVENT` instead of `5624`'s `HMI_AA_NOTIFY_KEY_EVENT` + `HMI_AA_NOTIFY_STANDBY_MODE`.
- Lacks `5624`'s `/proc/%d/task/` RT thread scheduling (`sched_setscheduler`), standby-mode audio/projection guards, and UI-event timeout callbacks.
