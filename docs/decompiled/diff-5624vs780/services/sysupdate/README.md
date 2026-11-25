[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/sysupdate/README.md)

# `opt/sysupdate` — Software Update & Flash Manager (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/sysupdate` (`728,396` B vs. `5624` `724,208` B, `.text +960` B, ARMv7-A ELF)
- **Decompiled**: `2,310` written / `2,699` total / `389` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#1-optsysupdate-960-b-text-build-string-12-30-2014--06-08-2017)

## Behavioral Differences vs. `5624_vw`

- **Mandatory RSA-2048 / SHA-256 package signing**: `IsPackageValid` ([`0x12fa8`](functions/0x12fa8.c)) calls `GetSignFileNameAndChecked` ([`0x1bce4`](functions/0x1bce4.c)) and `rsa_verify` ([`0x1c194`](functions/0x1c194.c)) using `gen_digest_by_file` ([`0x1bfac`](functions/0x1bfac.c)) and the 259-byte RSA-2048 public key read from `/dev/mtd0` (`GetPublicKey` [`0x1bbfc`](functions/0x1bbfc.c) → `load_pubkey_from_BSP` [`0x1be24`](functions/0x1be24.c)); returns error `7` (`Signature Error`) on failure.
- **HAB-compatible kernel CRC**: `CheckKernelImageValid` ([`0x1af8c`](functions/0x1af8c.c)) hashes `uImage` over `ih_size` (`0x40..0x40+ih_size`) rather than `file_len - 0x40`, tolerating the appended `8,520`-byte HAB trailer.
- **Removed debug / legacy hooks**: Removes `OnUSBUpdate` (`stdplus_debug_script.YFVE` and BT-only `.tar` special case) and `ReadVipImage()`'s 4-byte checksum path. Build date string `"12-30-2014"` → `"06-08-2017"`.
