[← 5606 Diff Index](../README.md) · [5624 Baseline](../../5624/hmi/README.md)

# `opt/hmi` — Main Headunit UI Application (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/hmi` (`10,282,460` B vs. `5624` `9,832,364` B, Δ `+450,096` B, ARMv7-A ELF, SHA-256 `7296a9007729e9b47a397cfaa152d14b1316253190744178c137a86f2ec3cf58`)
- **Decompiled**: `9,424` written / `9,910` total / `485` external / `1 (`_INIT_3` @ `0x11680`)` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Older `7.1.2.2` GP C++ HMI codebase (`.text −293,572` B, `.rodata +750,056` B; `39,367` texture slots / `2,611` unique `_ovg.bin` vs. `5624`'s `32,110` / `2,806`).
- Škoda green UI palette and `SmartLink` branding (`2,638` assets in `opt/gresfiles/`).
- Lacks 8 `.cpp` modules added in `5624`: anti-theft SAFE Code input/popups (`vsystemsafecodeinputpage.cpp`, `vsafecode*popuppage.cpp`), multi-language speller setup (`vsystemsetup_AddSpellerLanpage.cpp` for `BR`/`MEX`/`RU`), `vcameranotifyfilter.cpp`, `vDropDownListCtrl.cpp`, and `vphonecallextpop.cpp`.
- Lacks `5624` features: Voicemail/MailBox (`OnBTSetMailBoxCmd`), steering-wheel & window heater climate slots, Summer Time (DST), Android Auto standby mode (`SendAAStandbyMode`), CarPlay end timeout, USB-reset audio hook, and Russian/Mexican Spanish UI language buttons.
- Community `DE_CV` German overlay (`2019-08-01`): in-place hex-patch in compressed string table (`file 0x37bd7f` / `VA 0x383d7f`, base `VA 0x383ba0`, max start id `0x20c2c`) overwriting Slot 2 (`Português (BR)`) with `Deutsch (DE)  \x1f\x01\xde` while preserving `pBtnLangBRAZIL` → `1OnBtnLangBRAZILClicked` (`VA 0x3d5b44`).
