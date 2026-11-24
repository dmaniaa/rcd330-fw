# Community & Online Sources Synthesis (`RCD 330 Noname / Visteon`)

This document synthesizes the archived community articles in [docs/references/](references) and cross-references every hardware and software finding against our direct Ghidra 12.2 decompilation of `5624_vw` and `5606_skoda`.

Sources archived in this repo:
- [`freebuf_129160_zh.md`](references/freebuf_129160_zh.md) — *mengxp*, "大众车机天宝187A Hack笔记" (FreeBuf, 2017-03-16)
- [`reddit_uploading_files_bur5q0.md`](references/reddit_uploading_files_bur5q0.md) — `u/571Pro` on `r/RCD_330` ("Uploading files to the RCD 330 Noname/Visteon")
- [`reddit_language_translations_bh6p0j.md`](references/reddit_language_translations_bh6p0j.md) — `u/szyzu` & `u/Current1602` on `r/RCD_330` ("WIP language translations")
- [`reddit_wiki_index.md`](references/reddit_wiki_index.md) — `r/RCD_330` hardware & firmware wiki index

---

## 1. Hardware Architecture (from `mengxp` Teardown + Kernel/Device Evidence)

1. **Main SoC (ApP — Application Processor)**:
   - NXP/Freescale **i.MX6 DualLite / Solo** (`MCIMX6S6AVM08AC`, ARM Cortex-A9 @ up to 1 GHz, Visteon Eagle board `MACH_TYPE 4400`, integrated Vivante 2D/3D/OpenVG GPU).
   - **DRAM**: 256 MB on early `187A` (`Nanya NT5CB128M16FP-D1H`), upgraded to **512 MB DDR3** on `187B/C/E/F` (required for stable `5xxx` builds with CarPlay & Android Auto).
   - **NAND Flash**: Spansion `ML02G100BHA00` (256 MB x 8-bit).
2. **MTD Partition Layout (`/proc/mtd` & `df -k`)**:
   - `mtd0` (`4 MB`): `bootloader` (NXP i.MX6 U-Boot, flashed via `kobs-ng init`)
   - `mtd1` (`12 MB`): `nand.kernel` (`kernelImage` `uImage`)
   - `mtd2` (`208 MB`): `nand.rootfs` -> `ubi0:rootfs` UBIFS mounted at `/`
   - `mtd3` (`10 MB`): `pss1` -> `ubi1:pss1` mounted at `/pss1` (persistent coding, BT state, `stdplus.ini`, `hmi_autotest.ini`)
   - `mtd4` (`10 MB`): `pss2` -> `ubi2:pss2` mounted at `/pss2` (backup PSS partition + `app_data/`)
   - `mtd5` (`12 MB`): `logo` (raw splash framebuffer logo partition; sellers overwrite via `dd of=/dev/mtdblock5`, and `opt/hmi` `VETMHmiLookFeelPage` @ [`0x1fb9b8`](decompiled/hmi/functions/0x1fb9b8.c) can flash it from SD card at offsets `0x0` and `0x300000`)
3. **Vehicle Interface Processor (VIP MCU)**:
   - Renesas **RH850** (`R7F7010253`, QFP-100, 1 MB flash updated via `vipImage.bin` over SPI).
   - **Responsibilities**: CAN bus transceiver interface (PQ/MQB), part number / brand / VIN / coding identity, **power regulator enable (`EN`) sequencing**, and a **hardware watchdog (~60 s reset cycle)** fed by the SoC.
   - **IPCL over SPI**: Communicates with `opt/vgw` via `libHAL.so.1.0.0` (`spi_ipcl_write` / `spi_ipcl_read` over `/dev/spidev0.0` + `/dev/v850srq` + GPIO 83).
4. **Other On-Board Subsystems**:
   - **Touchscreen Controller**: Atmel **maXTouch T100** (`atmel_mxt_ts`) on I2C4 (`0x021f8000` / `/dev/i2c-3`, slave `0x4b`, GPIO1_27 IRQ), configured via `/lib/firmware/maxtouch.cfg` and calibrated with `tslib` (`/etc/pointercal`).
   - **Video Decoder**: Analog Devices `ADV7182` on I2C4 (`0x21`, CVBS reverse camera -> V4L2 `/dev/video0` & overlay `/dev/video17`).
   - **Bluetooth Module**: ALPS module (`BT_VER = C210`, firmware in `bt_ac_firm/`), connected via UART and managed by `opt/bt`.
   - **UARTDebug Console**: `18-pin` unpopulated header / `KEY_ROW0` & `KEY_COL0` pads (`W5`/`V6`), U-Boot checks `bootdelay=0` (requiring `bootcmd=` patch or script bypass to enter U-Boot prompt).

---

## 2. IPC & Watchdog Findings (`mengxp` vs. Our Decompilation)

`mengxp` discovered how Watchdog feeding and VIP resets cross process boundaries, which we confirmed directly in `opt/pwr_agent`, `opt/sysupdate`, `opt/vgw`, and `opt/hmi`:
- **VMF Wire Header (`26` byte header + payload)**:
  - Offset `0x00`: `0x02` (`uint8_t`, fixed packet marker)
  - Offset `0x14` (`20`): `groupid` (`uint8_t`) — e.g. `0x8F` (`143`) for HMI/system->VIP, `0x8E` (`142`) for power agent
  - Offset `0x15` (`21`): `eventid` (`uint8_t`)
  - Offset `0x18` (`24`): `payload_len` (`uint16_t` LE)
  - Offset `0x1A` (`26`): `payload[]`
  - Confirmed identically in `VBaseProxy::PackMsg` (`opt/hmi` [`0x5fe68`](decompiled/hmi/functions/0x5fe68.c)) and `opt/vgw` (`0x8b78`..`0xa320`).
- **Watchdog Feed (`pwr_agent`)**: `groupid = 0x8F (143)`, `eventid = 0x03`, `len = 4`, payload `[0x02, seq++, 0x01, vip_enabled]`.
- **Update Shutdown / Hard Reboot (`sysupdate`)**: Calling `SendUpdateStatus(1)` sends `groupid = 0x8F (143)`, `eventid = 0x19 (25)`, payload `[0x01]`, or via CLI: `/opt/tools/vmf_send 143 25 1`. VIP disables the watchdog during update reboot.

---

## 3. Unsigned Update Mechanism (`reddit_uploading_files_bur5q0.md` vs. `opt/sysupdate`)

Community members (`571Pro`, `mengxp`) documented that Noname/Visteon update `.tar` files have **no cryptographic signature verification**. Our decompilation of `opt/sysupdate` (`FUN_000147fc` `IsPackageValid` and `FUN_000190fc` `ExtractFileToDir`) confirms the exact preconditions:
1. `sysupdate` executes `tar -xvf "<pkg>.tar" <member> -C /sysupdate/system/`.
2. Required files inside the `.tar`:
   - `opt/stdplus.ini`: `[Upgrade] PACK_TYPE` **must match** the unit (`VW_PQ_PSS`), while `[Version] AP_VER` must differ from the currently installed `AP_VER` to avoid version-conflict rejection.
   - `config.txt`: specifies `kernel_update`, `bootloader_update`, `vip_update`, `bt_update`, `middleware_update` (`0` or `1`) and `[file]:[<dir>]` directory trees (`opt`, `etc`, `usr`, `mirrorlink`, `lib/firmware`) to extract.
   - `script/before_app_update.sh` & `script/after_app_update.sh`: shell scripts executed during the update cycle.
3. Custom overlays and third-party seller branding mods place payloads into `script/` or `opt/` and copy/remove files in `before_app_update.sh` (or overwrite `mtd5` for boot logos).

---

## 4. UI Assets (`gresfiles`) and Multi-Language Table (`reddit_language_translations_bh6p0j.md`)

- **Community findings (`szyzu`, `Current1602`, `r/RCD_330` wiki)**:
  - Identified that `opt/hmi` stores 5 languages inline between `"main RVC..."` and `"ts_open error"` using length-prefixed chunks and `\x1f` backreferences.
  - Community translators (`5606_skoda` German mod) had to hex-edit strings **in-place** inside `.rodata` (padding with spaces or `\x1f` refs) because hardcoded 16-bit/32-bit byte offsets inside `.text` point directly into the compressed blob.
  - Because German replacements in `5606_skoda` were hex-edited manually, a few slots (`0xe05d`, `0xfa29`, `0x10073`, `0x129f7`) drifted across multi-byte chunk headers (`mode == 3`), causing shifted codepages when decoded sequentially.
- **Our complete resolution ([`0x23650`](decompiled/hmi/functions/0x23650.c), [`0x325728`](decompiled/hmi/functions/0x325728.c))**:
  - Decoded 100% of the proprietary string compression opcode table (bits `[7]` = continuation, `[6:5]` = BMP page mode `0:0x00xx, 1:0x01xx, 2:0x04xx Cyrillic, 3:next-byte page`, `[4:0] == 0x1F` = 16-bit/24-bit backreference) into [resources/translations/](../resources/translations).
  - Decoded 100% of the IGDT 24-byte `_ovg.bin` descriptor table and PackBits RLE codec (`2,806/2,806` unique textures in `5624_vw`, `2,611/2,611` in `5606_skoda`) and scene-graph tables into [resources/scenes/](../resources/scenes).
