[← Decompiled Firmware Index](../../README.md)

# `opt/sysupdate` — Software Update & Flash Manager (`5624_vw`)

- **Target binary**: `opt/sysupdate` (`724,208` bytes, ARMv7-A Linux ELF, SHA-256 `9444c91c3ebc2837333b2366b9ceba5981efe6254221dd645ea6ba9b322d0982`)
- **Decompiled**: `2,267` written / `2,637` total / `370` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [firmware-packaging-and-update.md](../../../firmware-packaging-and-update.md) · [coding.md](../../../coding.md)

## Overview

Started early by `/opt/launch.sh` (and standalone by `/etc/rc.d/rcS` when `/sysupdate/flag.ini` requests an active upgrade). Handles USB/SD `.tar` firmware discovery, validation, backup/recovery, standalone update progress GUI (bundles its own lightweight IGDT/CxImage UI), and flashing of all headunit partitions:
- **Normal Mode**: Communicates with `opt/hmi` (`VServiceProxy`) over VMF `0xBD`/`0xBE`. Scans mounted USB/SD media, extracts `config.txt`, `opt/stdplus.ini`, and `script/` via `tar -xvf` without cryptographic signature verification, validates `[Upgrade] PACK_TYPE` (`VW_PQ_PSS`) and version diff (`[Version] AP_VER`), writes `/sysupdate/flag.ini`, and triggers a VIP watchdog reboot via `VMF 0x8F:0x19 [0x01]`.
- **Update Boot Mode**: Runs `script/before_app_update.sh`, flashes enabled targets (`u-boot.bin` via `kobs-ng`, `kernelImage` to `mtd1`, `vipImage.bin` over SPI, ALPS BT firmware `bt_ac_firm`), untars `[file]:[...]` rootfs trees with backup/rollback under `/sysupdate/backup/`, preserves `/pss1/stdplus.ini` coding keys (`BT`, `RVC`, `CODINGS`), and runs `script/after_app_update.sh`.

## Recovered Source Files

- `src/update_main.cpp`, `src/update_vmf.cpp`, `src/update_common.cpp`, `src/hot_plug.cpp`
- `src/nand_kernel_update.cpp`, `src/bt_upgrade/BtUpdateIf.cpp`, `src/bt_upgrade/BtUpdateOsapi.cpp`
- `src/interal_gui.c` & `src/display/*` (embedded standalone update-screen renderer)

## Key Functions

- [`0xf1b8`](functions/0xf1b8.c) — `main`: detects normal vs. update-boot mode and initializes the update service.
- [`0x147fc`](functions/0x147fc.c) (`IsPackageValid`), [`0x10fac`](functions/0x10fac.c) (`ParseConfigFile`), [`0x11f70`](functions/0x11f70.c) (version comparison), [`0x190fc`](functions/0x190fc.c) (`ExtractFileToDir`).
- [`0x12ba4`](functions/0x12ba4.c) (`MountUsb`), [`0x12ca8`](functions/0x12ca8.c) (`ScanUsbInDev`), [`0x12fe8`](functions/0x12fe8.c) (`BackupHostUpdateApp`), [`0x1349c`](functions/0x1349c.c) (`HostKernelUpdate`).
- [`0x16028`](functions/0x16028.c) — Preserves `[CODING]` keys (`BT`, `RVC`, `CODINGS`) in `/pss1/stdplus.ini` across upgrades.
- [`0x104e8`](functions/0x104e8.c), [`0x105b0`](functions/0x105b0.c), [`0x10684`](functions/0x10684.c), [`0x10758`](functions/0x10758.c) — `BackUpDTCBTInfo`, `BackUpDTCBAPInfo`, `UpdateBtVerInBtUpdate`, `SetRecoveryFlag`.
