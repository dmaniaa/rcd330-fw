[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/bt/README.md)

# `opt/bt` — Bluetooth Service (ALPS Stack) (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/bt` (`670,404` B vs. `5624` `680,932` B, Δ `-10,528` B, ARMv7-A ELF, SHA-256 `0392f8f686b76264b2ce5ef2712d4fa001d7f7d5d6d31578c4686b667523d5d9`)
- **Decompiled**: `1,515` written / `1,671` total / `156` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Older Nov 24, 2016 BT build (`−9,164` B `.text`, `−1,820` B `.rodata`).
- Lacks `5624`'s **Russian phonebook indexing** (`nbr_russian_f`), **Voicemail/MailBox** (`GetMailBox`/`SetMailBox`, `mailbox` column in `pb.db`), and **A2DP smartphone-projection coexistence** (`CheckA2DPAppCoExist`, `A2DPDevID`/`HFPDevID` tracking across CarPlay/AA/CarLife).
- Retains `SendAlpsSendSppDataReq` and `pim_PbdlCallhistoryEntryHandle` (`[CallHistory Filter: No Tel]`).
