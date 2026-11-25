[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/carplay_App/README.md)

# `opt/carplay_App` — CarPlay Session & UI Coordinator (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/carplay_App` (`120,060` B vs. `5624` `116,508` B, `.text +1,176` B, ARMv7-A ELF)
- **Decompiled**: `228` written / `305` total / `77` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#4-optcarplay_app-1176-b-text)

## Behavioral Differences vs. `5624_vw`

- **Nested screen-borrow reference counting**: Adds `i_ScreenBorrowCmdNestCnt` (max depth `5`, reset to `-1` on `CMD_HMI_EXIT_CARPLAY`, `CMD_CPS_RELEASE_VIDEO[_SUCCESS]`, and `nResetVars`) with watchdog `CPA_StartCheckingBorrowNestedTimer` / `CPA_StopCheckingBorrowNestedTimer` (bit `0x40` in main loop [`0x9928`](functions/0x9928.c) injects event `0x21`). Replaces `5624`'s `start_source_off_timer`/`stop_source_off_timer`.
- **BT Siri/Phone readiness handler**: Adds `HandleVmfMsgFromBT` ([`0x14490`](functions/0x14490.c)) mapping BT sub-messages `1 → CPA_EVENT_BT_READY_SIRI (0x4a)` and `2 → CPA_EVENT_BT_READY_PHONE (0x4b)`.
- **Playback guards**: Ignores `CMD_HMI_PAUSE` while media is not yet playing and adds a NULL-pointer check on `vp_data`.
