[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/diag_eol/README.md)

# `opt/diag_eol` — End-of-Line Factory Diagnostics (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/diag_eol` (`24,956` B vs. `5624` `19,568` B, `.text +3,720` B, ARMv7-A ELF)
- **Decompiled**: `106` written / `137` total / `31` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#2-optdiag_eol-3720-b-text)

## Behavioral Differences vs. `5624_vw`

Adds four security provisioning subcommands under `DIAG_EOL_ITEM_SYSTEM` ([`0xa544`](functions/0xa544.c)):
- **`0x05` / `0x06` (`EOL_CMD_SYSTEM_{WR,READ}_SECURITY_CODE`)**: Persists an 8-byte security code to PSS Block `100` (`0x64`) and updates the Linux `root` password via `diag_eol_secririty_code_write` ([`0x96a0`](functions/0x96a0.c): hex-encodes 8 B in [`0x95fc`](functions/0x95fc.c), remounts `/` rw, writes `root:<16-hex>` to `/pss1/config/password.txt`, invokes `chpasswd -s`, removes the file, and remounts `/` ro).
- **`0x1d` (`DOWNLOAD_PUBLIC_KEY`)**: Stores a 32-byte HAB SRK hash in PSS Block `101` (`0x65`).
- **`0x1e` (`READ_PUBLIC_KEY`)**: Reads Block `101`, pre-writes byte-swapped 32-bit words to `/sys/fsl_otp/HW_OCOTP_SRK0..7` ([`0x98f4`](functions/0x98f4.c)), and sets armed flag `DAT_00015f2e = 1`.
- **`0x1f` (`BURN_PUBLIC_KEY`)**: Writes `0x00040000` to `HW_OCOTP_MEM0` and blows `HW_OCOTP_CFG5` ([`0x9be0`](functions/0x9be0.c)), syncs, and reboots *(stock quirk: logs `"sleep 3s"` while calling `usleep(1000)` three times = 3 ms)*.
