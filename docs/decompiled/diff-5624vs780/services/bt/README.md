[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/bt/README.md)

# `opt/bt` — Bluetooth Service (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/bt` (`678,596` B vs. `5624` `680,932` B, `.text −2,064` B, ARMv7-A ELF)
- **Decompiled**: `1,524` written / `1,680` total / `156` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#5-optbt-2064-b-text--6-optmedia-516-b-text)

## Behavioral Differences vs. `5624_vw`

- **USB iPod / BT A2DP conflict arbitration (added vs. `5624`)**: `tm_fun_core_media_name_info` ([`0x454c4`](functions/0x454c4.c)) and `tm_fun_core_media_remove` track the connected USB iPod received from `opt/media`; `ReceiveAlpsAuthoriseInd` ([`0x31034`](functions/0x31034.c)) rejects A2DP (`uVar1 & 8`) when the BT device name matches the connected USB iPod (`"Same device with Ipod"`).
- **Additional guards in `8.5.9.7`**: Reconnect rejection in `AbstractDeviceExistDeviceAuthorise` during active disconnect (`PowerFlag == 1`, `state == 4 && enOperation == 2`), combined phonebook completion check (`pbdl_ProcessBecameEnd` [`0x65b18`](functions/0x65b18.c)), ALPS health-monitor query (`tm_fun_core_get_healthMonitor_moduleState` [`0x3b5d0`](functions/0x3b5d0.c)), and AVRCP root-browse `followmodeflag`/`chgroot` guard (`tm_bt_cmd_audio_root_brw` [`0x20704`](functions/0x20704.c)).
- **Absent vs. newer `5624`**: Lacks `5624`'s Voicemail/MailBox feature (12 functions including `SetMailBox`/`GetMailBox` and the `pb.db` `mailbox` column).
