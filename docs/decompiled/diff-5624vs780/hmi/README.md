[← RCD 780 Diff Index](../README.md) · [5624 Baseline](../../5624/hmi/README.md)

# `opt/hmi` — Main Headunit UI Application (RCD 780 / `8.5.9.7` CR222 + `5814` DE Overlay)

- **Target binary**: `opt/hmi` (`9,874,228` B vs. `5624` `9,832,364` B, `.text +21,176` B, `.rodata +20,960` B, ARMv7-A ELF, SHA-256 `593bb4de8eeef35ffe0bcac4436c3c21871e96e02fdb19b03fb31461b6abfcd5`)
- **Decompiled**: `9,606` written / `10,091` total / `484` external / `1` failed (`_INIT_3` @ `0x11658`) in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../diff-5624-vs-rcd780.md#5-opthmi-deep-dive-official-cr222-8597-changes-vs-aftermarket-german-overlay)

## Behavioral Differences vs. `5624_vw`

### Official Visteon `CR222 8.5.9.7` Changes
- **Update Signature Error**: `VUpdatePage` ([`0x1ee4cc`](functions/0x1ee4cc.c), [`0x1ee4f8`](functions/0x1ee4f8.c), [`0x1ee7d0`](functions/0x1ee7d0.c), [`0x1ee8b8`](functions/0x1ee8b8.c)) handles `cVar1 == 6` (`sysupdate` error `7`) and displays `"Signature Error"`.
- **Rear Traffic Alert (RTA)**: Adds `VBAPProxy::onSetRTAActive` ([`0xb0c34`](functions/0xb0c34.c)), `VCarPdcSetupPage` ([`0x37b8f0`](functions/0x37b8f0.c), `pBtnRTAAtive` / `OnBtnRTAActiveClicked`), and OPS left/right RTA warning bars in `vcar_ops` ([`0x14c5c8`](functions/0x14c5c8.c)).
- **FPA Driving Mode Enhancements**: Adds Snow mode (`VCarFpaPage::OnSnowClicked` [`0x29ce94`](functions/0x29ce94.c)), Race artwork, `SwitchFPAPopType` ([`0x29f514`](functions/0x29f514.c)), and Climate-on-FPA coordination (`SetShowClimateOnFPAFlag` [`0xbf70c`](functions/0xbf70c.c) / `GetShowClimateOnFPAFlag` [`0xbf778`](functions/0xbf778.c)).
- **Touchscreen Climate Temperature Controls**: `vClimate_main` ([`0x279f88`](functions/0x279f88.c)) and `VBAPProxy` ([`0xb98d8`](functions/0xb98d8.c)) add Driver/Co-driver temperature +/- buttons; `vClimate_setup` ([`0x292da4`](functions/0x292da4.c)) expanded.
- **Composite Faceplate Key & MQB Assets**: `VIPCLNotifyFilter` ([`0x43a2c`](functions/0x43a2c.c)) registers `{5, 1}` (PHONE+RADIO, mode `9`); `VPowerProxy::mqbOpsCarType` ([`0x5b69c`](functions/0x5b69c.c)) adds `VHCL_VW3165CS_KL`, `VW4211CS_K`, `VW3130CS_K`, `VW2700CS_K1`, `VW2160CS_K`; `vappconnectinfo.cpp` ([`0x34a618`](functions/0x34a618.c)) splits GP VW vs. ROW artwork.
- **Other `8.5.9.7` updates**: `VSoundSetupMain::UpdateEQDisplayStatus` ([`0x1b6fc8`](functions/0x1b6fc8.c)), SMS display timer ([`0xefe98`](functions/0xefe98.c)), BT contacts download check ([`0xcbf98`](functions/0xcbf98.c)), MirrorLink USB VID logging ([`0xdeb94`](functions/0xdeb94.c)), `VMediaBrwPage::MSG_MEDIA_APP_PLAY_EVENT_RET` ([`0x119790`](functions/0x119790.c)), Start-Stop clutch prompt ([`0x376470`](functions/0x376470.c)), and string decoder [`0x23650`](functions/0x23650.c) bounds widened (`0x20fcc/0x2cf → 0x21156/0x2e1`, table base `VA 0x3d0938`).
- **Absent vs. newer `5624`**: Lacks `VBTProxy::OnBTSetMailBoxCmd`, `VDiagProxy::DiagThresholdHighNotify` `case 0x1b`, and AA speed-limit coding (`0x38ab54` still exposes `uc_shutoff_bt_bonding`).

### Aftermarket German + `5814` Overlay (`.rodata` In-Place Hex-Patch)
- Renames `Português (BR)` (`VA 0x3d0b1c`) to `Deutsch (DE)\x01)`.
- Hex-patches `pBtnLangBRAZIL`'s Qt `SLOT(...)` string (`VA 0x426130`, wired in `VSystemSetupLanguageSelectPage` [`0x16b790`](functions/0x16b790.c)) from `1OnBtnLangBRAZILClicked(...)` to `1OnBtnLangChineseClicked(...)` so tapping "Deutsch (DE)" activates Slot 1 (`zh_CN`).
- Replaces Slot 1 (`zh_CN`) with German (overflowing long strings into Slot 2 `pt_BR` while preserving Slots 3/4) and references 41 custom whole-word `U+FFxx` glyphs embedded in `FZHT_GB18030.ttf` (`253` occurrences vs. `3` in `5624`).
