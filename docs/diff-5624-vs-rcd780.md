# Firmware Diff: Stock VW `5624` (`5624_vw`) vs. RCD 780 Dump (`8.5.9.7` CR222 + `5814` DE Overlay)

Complete static and Ghidra 12.2 decompilation comparison between stock Noname/Visteon RCD 330 VW firmware `5624` (`extracted-fw/5624_vw/`, decompiled under [docs/decompiled/5624/](decompiled/5624/README.md)) and a full NAND/rootfs dump of an aftermarket RCD 330 clone marketed as **"RCD 780"** (`original-fw/rcd780_sysupdate_emu.zip` → `dump/{mtd,rootfs}`, all `12` differing core binaries archived under [docs/decompiled/diff-5624vs780/](decompiled/diff-5624vs780/README.md)).

---

## Executive Summary & Build Identification

The RCD 780 dump is **not a clean-room clone firmware**. It is an **official Visteon VW MQB firmware build (`8.5.9.7`, `CR222`)** with an **aftermarket seller/community German & `5814` customization overlay** applied on top, running on hardware whose Renesas RH850 VIP MCU identifies itself as a **VW PQ (`6RD035187B`)** headunit:

1. **Base Firmware (Layer 1 — Official Visteon MQB `CR222` Build `8.5.9.7` / `181124`)**:
   - Proven directly by `/sysupdate/update_info.ini`:
     ```ini
     PACK_NAME=std_plus_pss-6.5inch_181124-8.5.9.7_VW_MQB_CR222_sig.tar
     PACK_STATUS=UPDATED
     BT_VER=C210
     BAP_VER=V2.26.321.14
     ```
   - Corroborated by DWARF debug paths inside `opt/md_adapter` (`/var/lib/jk/stdplus/workspace/STDPLUS_6.5INCH_HOST_CR222_Generate/...` vs. `5624`'s `STDPLUS_6.5INCH_HOST_SOP_ROW_Generate`), `opt/feature_config.ini` (`PROJECT=ROW`, `PLATFORM=MQB`, `BRAND=VW`, `SUBPROJECT=CR222`), and uniform build mtimes of **`2018-11-23 18:03–18:04 UTC`** across `opt/` and `opt/gresfiles/`.
   - Ships with NXP i.MX6 **HAB (High Assurance Boot)** across U-Boot (`Bootloader--1.4.6-ga85c84d`, `2017-09-26`), the Linux kernel (`3.0.35-8.0.4-gdffba54`, `2017-03-15`, with an `8,520`-byte HAB CSF/IVT trailer), mandatory RSA-2048/SHA-256 update signature verification in `opt/sysupdate`, and HAB SRK/root-password provisioning commands in `opt/diag_eol` + `opt/pss`.
2. **Aftermarket Overlay (Layer 2 — Post-Nov 2019 German + `5814` Customization)**:
   - A small set of **9 files** under `opt/` and `opt/gresfiles/` carry anomalous mtimes of **`2016-04-24 10:05–10:19 UTC`** (written on a unit whose uninitialized Linux RTC booted at the `2016-04-24` epoch), while `opt/gresfiles/FZHT_GB18030.ttf`'s embedded `name` table proves the overlay was created on or after **November 28, 2019** (`Version 1.00;November 28, 2019;FontCreator 11.5.0.2427 64-bit`).
   - This overlay rewrites `opt/stdplus.ini` (and `pss1/stdplus.ini`) to display spoofed version numbers (`CUSTM_PQ=5814`, `CUSTM_MQB=5814`, `AP_VER=5.4.9.2`, `BT_VER=C370`), in-place hex-patches `opt/hmi`'s `.rodata` to replace Chinese (Slot 1) with German and reroute the `Português (BR)` language button to `OnBtnLangChineseClicked`, and embeds **41 custom whole-word German ligature glyphs** at `U+FFxx` inside `FZHT_GB18030.ttf` to work around fixed-length compressed string slots.
3. **VIP Hardware Mismatch (MQB Rootfs on PQ-Reporting VIP)**:
   - The runtime `[VIP]` section of `opt/stdplus.ini` reports `PLAT=PQ`, `BRAND=VW`, `PARTSTR="6RD035187B "`, `VIPAP_VER=5.5.6.2`, `VIPBL_VER=2.0.4.0`, `BAP_VER=V2.25.337.12`, and `BT_VER=C210` — showing an MQB `CR222` Linux image paired with a PQ-reporting VIP MCU, a common pattern on aftermarket RCD 360/780 conversions.
4. **Branch Relationship to `5624` (`AP_VER=8.8.9.1`)**:
   - `8.5.9.7` (`CR222` MQB, Nov 2018) and `5624` (`8.8.9.1` SOP ROW PQ, 2019) are **sibling `8.x` branches**, not strict parent/child. `8.5.9.7` contains MQB/HAB/security features, newer MirrorLink (`AllGoMLC 7.5.7` vs. `7.5.3`), CarPlay nested screen-borrow handling, and USB iPod/BT A2DP conflict arbitration, whereas `5624` (`8.8.9.1`) contains later PQ/ROW additions absent from `8.5.9.7` (BT Voicemail/MailBox in `opt/bt` + `opt/hmi`, Android Auto framing-error disconnect recovery in `aap_service` + `AAP_Application`, `mdnsd` Dec 2016, and AA speed-limit coding fields).

---

## 1. High-Level Dump, MTD Flash & Filesystem Overview

### Dump Contents (`dump/`)

| Artifact | Size (B) | MD5 | Notes |
|---|---:|---|---|
| `mtd/mtd0.bin` | 4,194,304 | `63ca824fe6c82b71b5a042e8b471ccda` | HAB-signed U-Boot (`Bootloader--1.4.6-ga85c84d`), dual identical copies at `0x100000` and `0x280000` |
| `mtd/mtd1.bin` | 12,582,912 | `4088af46cc504db93482d7628f820ebf` | `uImage` (`Linux-3.0.35-8.0.4-gdffba54`) + `8,520`-byte HAB signature trailer (`0x2d6ed8..0x2d9020`) |
| `mtd/mtd5.bin` | 12,582,912 | `4fa41a2c83d3182475b962b284f97c71` | Boot splash logo: dual identical `800×480` BGRA VW chrome emblems (`"WELCOME TO VOLKSWAGEN"` on dark dotted world map) at `0x0` and `0x300000` |
| `rootfs/` | 7,173 entries | — | Full mounted rootfs (`bin`, `etc`, `home`, `include`, `lib`, `mirrorlink`, `opt`, `pss1`, `pss2`, `root`, `sbin`, `share`, `sysupdate`, `tmp`, `usr`, `var`) |

### Shared Subtree Diff (`opt/`, `etc/`, `usr/`, `lib/`, `mirrorlink/`, `var/`)

Comparing the subtrees present in the `5624_vw` update archive against the RCD 780 rootfs dump:
- **Identical files**: `3,356`
- **Changed files**: `229` (`195` in `opt/gresfiles/`, `18` user-space ELFs in `opt/`, `7` `.ko` modules in `opt/`, plus `opt/stdplus.ini`, `opt/feature_config.ini`, `opt/launch.sh`, `opt/config/{connect_mgr,pwr_agent,sysupdate}.conf`, and `etc/{passwd,group,shadow}`)
- **Only in `5624_vw`**: `74` (`68` in `opt/gresfiles/`, `opt/tools/vmf_send`, `opt/tools/vmf_rcv`, etc.)
- **Only in RCD 780**: `3,067` (primarily full LTIB rootfs system libraries/binaries under `/usr`, `/lib`, `/etc`, `/bin`, `/sbin` that are not packaged inside a delta update `.tar`, plus `210` new files in `opt/gresfiles/` and `opt/g_iap_ncm_eap.ko`)
- **Outside `opt/` and `mirrorlink/`**: All **`270` shared ELF binaries** (Qt 4.7.4, Vivante GLES, GStreamer, FreeType, system libs) are **100% byte-identical** to `5624_vw`.

### Bootloader (`mtd0.bin`) & Kernel (`mtd1.bin`)

| Component | `5624_vw` | RCD 780 (`8.5.9.7` CR222) | Behavioral Difference |
|---|---|---|---|
| **U-Boot** | `Bootloader--1.4.1-gafa5248` (`/home/changchun/projects/stdplus/bootloader_2009.08`) | `Bootloader--1.4.6-ga85c84d`, built `2017-09-26 15:35:47` (`/media/data/home/zhouqi/git/projects/stdplus-hab/u-boot85/bootloader_2009.08`) | Adds **NXP i.MX6 HAB** (`Authenticate uImage from DDR location 0x%lx...`), embeds CSF/IMG/SRK X.509 certificates (`CSF1_1_sha256_2048_65537_v3_usr`, `IMG1_1_sha256_2048_65537_v3_usr`, `SRK1_sha256_2048_65537_v3_ca`, validity `170825..270823` and `181126`), IVT at `0x100400` (`self=0x907400`, `csf=0x909400`), RSA-2048 public key (`e=0x10001`, modulus prefix `d68e73ec...`) at `0x102458` (also read at runtime by `opt/sysupdate`), and strips interactive fuse-blowing/serial-download U-Boot commands. |
| **Linux Kernel** | `Linux-3.0.35-5.1.10-g3847f9f` (`#10`, `2017-03-14 09:22:29 UTC`, `changchun@HP-640`, GCC 4.6.2) | `Linux-3.0.35-8.0.4-gdffba54` (`#7 SMP PREEMPT`, `2017-03-15 07:36:58 UTC`, same `changchun@HP-640`, GCC 4.6.2) | Decompressed `vmlinux` is the exact same size (`5,809,668` B) with identical `fsl_otp`/`HW_OCOTP`/`hab` symbols and only 7 differing version/initramfs-cpio strings. Main difference is an **`8,520`-byte HAB signature trailer** appended after `uImage` (`0x2d6ed8..0x2d9020`, `0x5a`-padded IVT at `0x2d7000`). |

### Kernel Modules (`.ko`)

| Module | `5624_vw` Size (B) | RCD 780 Size (B) | `vermagic` (`5624` → `780`) | Notes |
|---|---:|---:|---|---|
| `opt/asix.ko` | 25,916 | 25,916 | `3.0.35-00009-gafa8214` → `3.0.35-8.0.0-g6ac2571` | Rebuilt for `8.0.0` kernel |
| `opt/cdc_ncm.ko` | 17,998 | 17,998 | `3.0.35-7.6.7-g258ba5b` → `3.0.35-8.0.1-g1021ff7` | Rebuilt for `8.0.1` kernel |
| `opt/g_ether.ko` | 58,175 | 58,175 | `3.0.35-00009-gafa8214` → `3.0.35-8.0.0-g6ac2571` | Rebuilt for `8.0.0` kernel |
| `opt/g_file_storage.ko` | 48,584 | 48,584 | `3.0.35-00009-gafa8214` → `3.0.35-8.0.0-g6ac2571` | Rebuilt for `8.0.0` kernel |
| `opt/g_hid.ko` | 32,551 | 32,551 | `3.0.35-00009-gafa8214` → `3.0.35-8.0.0-g6ac2571` | Rebuilt for `8.0.0` kernel |
| `opt/usbnet.ko` | 33,518 | 33,518 | `3.0.35-7.6.7-g258ba5b` → `3.0.35-8.0.1-g1021ff7` | Rebuilt for `8.0.1` kernel |
| `opt/g_iap_ncm_audio.ko` | 58,824 | 70,501 | `3.0.35-7.6.7-g258ba5b` → `3.0.35-00009-gafa8214` | **Byte-identical to `5606_skoda`** (not loaded by `launch.sh`) |
| `opt/g_iap_ncm_eap.ko` | *(absent)* | 65,049 | `3.0.35-8.0.1-g1021ff7` | **Loaded by `opt/launch.sh`** instead of `g_iap_ncm_audio.ko` (similar to `5606_skoda`'s `65,057`-byte variant) |
| `mirrorlink/cdc_ncm.ko` | 17,982 | 17,982 | `3.0.35` (identical) | **Byte-identical** |
| `mirrorlink/usbnet.ko` | 33,502 | 33,502 | `3.0.35` (identical) | **Byte-identical** |

---

## 2. Configuration, Startup, System Accounts & PSS State

### `opt/stdplus.ini` (and `pss1/stdplus.ini`)

| Section / Key | `5624_vw` | RCD 780 (`opt/stdplus.ini` & `pss1/stdplus.ini`) | Notes |
|---|---|---|---|
| `[Version] AP_VER` | `8.8.9.1` | `5.4.9.2` | Spoofed by aftermarket overlay (`2016-04-24 10:05 UTC`); true base is `8.5.9.7` |
| `[Version] CUSTM_PQ` / `CUSTM_MQB` | `5624` / `5624` | `5814` / `5814` | Spoofed custom software version displayed in System Information |
| `[Version] UBOOT_VER` | `uboot_1.5.2` | `uboot_` | Truncated in overlay edit |
| `[Version] UIMAGE_VER` / `KERNL_VER` | `uImage_7.7.12` / `3.0.38` | `uImage_8.0.4` / `3.0.35` | Matches `mtd1.bin` (`3.0.35-8.0.4-gdffba54`) |
| `[Version] BT_VER` | `C210` | `C370` | Spoofed in `[Version]` (`update_info.ini` and `[VIP]` both confirm `C210`) |
| `[Version] VIPAP_VER` / `VIPBL_VER` | `8.8.9.2` / `2.1.5` | `5.4.6.5` / `2.0.3` | Package header metadata |
| `[Upgrade] PACK_TYPE` | `VW_PQ_PSS` | `VW_MQB_PSS` | MQB package family |
| `[CODING] BT` / `CODINGS` / `RVC` | `0` / `0` / *(unset)* | `1` / `1` / `1` | Runtime cached flags |
| `[VIP] PLAT` / `BRAND` / `PARTSTR` | *(not in tar)* | `PQ` / `VW` / `"6RD035187B "` | Runtime VIP MCU identity (`VIPAP_VER=5.5.6.2`, `VIPBL_VER=2.0.4.0`, `BAP_VER=V2.25.337.12`, `BT_VER=C210`) |

### `opt/feature_config.ini`

| Key | `5624_vw` | RCD 780 (`8.5.9.7` CR222) | Effect |
|---|---|---|---|
| `PROJECT` / `PLATFORM` / `BRAND` | `ROW` / `PQ` / `VW` | `ROW` / `MQB` / `VW` | Selects MQB BAP/OPS/Climate/FPA code paths in `opt/hmi` |
| `SUBPROJECT` | `NONE` | `CR222` | Enables CR222-specific car silhouettes and App-Connect variants |
| `BT`, `RVC`, `CARPLAY`, `CARLIFE`, `MLINK`, `NEWBAP`, `ETM`, `OPS`, `CLIMATE` `_ATTR` | `TRUE` (`CARLIFE_ATTR=INVALID`) | `FALSE` | Defers feature enablement to dynamic coding (`DIAG_PSS_CODING.pss`) and Feature Manager (`FEATURE_MANAGER_PSS_BLK.pss`) rather than hard-forcing them |
| `EXLAP_ATTR` / `ANDAUTO_ATTR` | `TRUE` / `TRUE` | `TRUE` / `TRUE` | Enabled in both |

### Startup, Logging, Tools & Accounts

- **`opt/launch.sh`**: Only one functional line differs — `insmod g_iap_ncm_audio.ko` (`5624`) becomes `insmod g_iap_ncm_eap.ko` (`780`).
- **`opt/config/{connect_mgr,pwr_agent,sysupdate}.conf`**: `ENABLE=1` in RCD 780 vs. `ENABLE=0` in `5624` (console/trace logging enabled).
- **Removed VMF CLI tools**: `opt/tools/vmf_send` and `opt/tools/vmf_rcv` are absent in RCD 780.
- **`etc/{passwd,group,shadow}`**: Stripped down from full LTIB user tables (`1,865` / `748` / `1,162` B in `5624`) to `root`-only (`30` / `12` / `84` B), with a salted SHA-256 root password (`$5$KavSUi...`) set via `diag_eol`'s security-code provisioning flow (see §4.2).
- **`sysupdate/flag.ini`**: Contains `FLAG_UPDATE_UNKNOW` (mtime `2016-04-24 10:05:14 UTC`).

### Runtime `/pss1/` Persistent Storage Highlights

| PSS Block File | Payload (Hex, after 8-byte PSS header) | Decoded Meaning |
|---|---|---|
| `DIAG_PSS_CODING.pss` | `17 00 02 00 02 00 04 00 51 11 00 01 00 0C 0A 00 2F 03 00 B4 01 20 01 00 03` (25 B) | MQB 25-byte VW Long Coding (see [coding.md](coding.md)): Byte 16 (`0x2F`) BT features enabled, Byte 18 (`0x00`) stock VW boot logo, Byte 19 (`0xB4`, bit 4 = `1`) RVC enabled |
| `FEATURE_MANAGER_PSS_BLK.pss` | `01 00 01 01 01 00 ...` (25 B) | Feature Manager flags: BT=`1`, RVC=`0`, CarPlay=`1`, CarLife=`1`, MirrorLink=`1`, remaining=`0` |
| `V_EOL_PSS_PUBLIC_KEY_BLK.pss` | `7f80586295b4a854c8ea2132a48e7b0580a0b7949003ee35747d020e5660224d` (32 B) | Provisioned HAB SRK SHA-256 hash (new PSS Block `101` / `0x65`) |
| `V_EOL_PSS_SECURITY_CODE_PASSWORD.pss` | `00 00 00 00 00 00 00 00` (8 B) | Security-code password slot (new PSS Block `100` / `0x64`, zeroed) |

---

## 3. User-Space ELF Binary Inventory (`opt/`)

### Byte-Identical Core Binaries (Unchanged from `5624_vw`)

The following core binaries and shared libraries under `opt/` are **100% byte-identical** between `5624_vw` and the RCD 780 dump:
- **IPC, HAL & Supervision**: `opt/vmf`, `opt/vgw`, `opt/pwr_agent`, `opt/hal_init`, `opt/lib/libHAL.so.1.0.0`, `opt/lib/libOSAL.so.1.0.0`, `opt/lib/libvmf_client.so`, `opt/lib/libvmf_trace.so`
- **Services**: `opt/diag`, `opt/camera`, `opt/screenshot`, `opt/connect_mgr`, `opt/carplayd`, `opt/iap2d`, `opt/mfid`
- **Libraries**: `opt/lib/libCarPlayAudio.so`, `opt/lib/libCarPlayPlatformSystem.so`, `opt/lib/libiap2.so`, `opt/lib/libmfi.so`, `opt/AndroidAuto/lib/*`, `opt/lib/bt/*`, `opt/lib/media/*`, `opt/lib/screenshot/*`

### All `18` Differing User-Space ELF Binaries

Every one of the 18 differing ELF binaries was inspected (full Ghidra 12.2 `decompile_all` for 13 binaries, targeted `batch_decompile` for `aap_service` and `opt/hmi`, and byte/disassembly verification for `exlap`, `mdnsd`, and `iperf`):

| # | Binary | `5624` (B) | `780` (B) | Size Δ | `.text` Δ | `.rodata` Δ | `780` mtime (UTC) | Summary of Difference |
|---:|---|---:|---:|---:|---:|---:|---|---|
| 1 | `opt/sysupdate` | 724,208 | 728,396 | +4,188 | +960 | +672 | `2018-11-23 18:04` | Mandatory RSA-2048/SHA-256 `.sig` verification (pubkey from `/dev/mtd0`), HAB-safe kernel CRC, debug USB hook removed |
| 2 | `opt/diag_eol` | 19,568 | 24,956 | +5,388 | +3,720 | +1,164 | `2018-11-23 18:04` | Adds `DIAG_EOL_ITEM_SYSTEM` security code (`chpasswd`) + HAB SRK OTP fuse download/read/burn commands |
| 3 | `opt/pss` | 53,964 | 54,156 | +192 | 0 | +192 | `2018-11-23 18:04` | Expands PSS table `100 → 102` blocks (adds Blocks `100`/`101` for `diag_eol` security code & SRK hash) |
| 4 | `opt/carplay_App` | 116,508 | 120,060 | +3,552 | +1,176 | +749 | `2018-11-23 18:04` | Adds nested screen-borrow refcount + timer, BT-ready Siri/Phone VMF handler, pause-when-not-playing guard |
| 5 | `opt/bt` | 680,932 | 678,596 | −2,336 | −2,064 | −440 | `2018-11-23 18:04` | Adds USB iPod / BT A2DP conflict rejection, PB completion & health checks; lacks `5624`'s Voicemail/MailBox feature |
| 6 | `opt/media` | 384,008 | 384,008 | 0 | +516 | +28 | `2018-11-23 18:04` | Sends connected iPod name (`0x37`) / removal (`0x17`) to `opt/bt` over VMF `(0x24, 0x02)` |
| 7 | `opt/audio_proc` | 168,636 | 168,940 | +304 | +284 | +20 | `2018-11-23 18:04` | Adds 2-bit ALSA/DSP teardown synchronization barrier when switching away from MirrorLink (`0xc → 8`) |
| 8 | `opt/carlife_daemon` | 285,928 | 286,236 | +308 | +344 | +56 | `2018-11-23 18:04` | Drains residual ALSA music `std::deque` on stop; resets header state on unknown MD message |
| 9 | `opt/md_adapter` | 292,387 | 293,227 | +840 | +408 | +104 | `2018-11-23 18:03` | `CR222` build; adds `argv[2]` length check (`<= 29`) and libusb NULL pointer guards |
| 10 | `opt/exlap` | 751,740 | 751,740 | 0 | 0 | 0 | `2018-11-23 18:04` | **Exact 1-byte diff** at `VA 0x1c0b4`: `cmp r12, #0x91` (`5624`) vs. `cmp r12, #0x90` (`780`) |
| 11 | `opt/AndroidAuto/bin/AAP_Application` | 154,596 | 154,648 | +52 | +52 | 0 | `2018-11-23 18:04` | Lacks `5624`'s `status == 0x113` framing-error disconnect check |
| 12 | `opt/AndroidAuto/bin/aap_service` | 1,741,800 | 1,741,480 | −320 | −256 | −40 | `2018-11-23 18:04` | Paired with #11: lacks `5624`'s framing-error stop path (`obj+0x804`) |
| 13 | `opt/mdnsd` | 378,928 | 378,924 | −4 | 0 | −1 | `2018-11-23 18:04` | **Byte-identical to `5606_skoda`** (`R2-3-gcdc9` Sep 2016 vs. `5624`'s `R2-4-g526e7` Dec 2016) |
| 14 | `opt/iperf` | 71,904 | 71,904 | 0 | 0 | 0 | `2018-11-23 18:03` | 4-byte linker symbol-table diff only (`.hash`/`.dynsym[86]`, identical `.text`) |
| 15 | `opt/lib/mirrorlink/libmlc_iface.so` | 1,266,543 | 1,307,116 | +40,573 | +24,984 | +14,960 | `2018-11-23 18:03` | Upgraded `AllGoMLC:1.1:7.5.3` → **`7.5.7`** (+6 exports, RTP silence gating, XML app-list & WFD updates) |
| 16 | `opt/lib/mirrorlink/libaupnp.so` | 58,070 | 59,250 | +1,180 | +796 | +360 | `2018-11-23 18:03` | AllGo `7.5.7` companion (M-SEARCH guard, cleanup on error, NULL check) |
| 17 | `opt/lib/mirrorlink/libupnp.so` | 165,814 | 166,830 | +1,016 | +924 | +4 | `2018-11-23 18:03` | `sock_read_write` explicitly treats `numBytes == 0` (peer close) as error `-0xd0` |
| 18 | `opt/hmi` | 9,832,364 | 9,874,228 | +41,864 | +21,176 | +20,960 | **`2016-04-24 10:16`** | Official `CR222 8.5.9.7` MQB UI features (RTA, FPA Snow/Race, Climate temps, Sig Error) **+ aftermarket German hex-patch** |

---

## 4. Detailed Decompiled Behavioral Differences (Binaries #1–#17)

### 4.1 Security, Update Signing & Factory Provisioning

#### 1. `opt/sysupdate` (`+960` B `.text`, build string `"12-30-2014"` → `"06-08-2017"`)
Reference `5624` decompilation: [docs/decompiled/5624/services/sysupdate/README.md](decompiled/5624/services/sysupdate/README.md).
- **Mandatory RSA-2048 / SHA-256 package signature verification**:
  - `IsPackageValid` (`780 0x12fa8`) now calls `GetSignFileNameAndChecked` (`0x1bce4`, verifying the companion `.sig` file exists) followed by `rsa_verify` (`0x1c194`).
  - `rsa_verify` hashes the update archive with SHA-256 via `gen_digest_by_file` (`0x1bfac`) and verifies the signature via OpenSSL `RSA_verify(NID_sha256=0x2a0, ...)` using a 259-byte DER/raw RSA-2048 public key extracted directly from the U-Boot HAB CSF in `/dev/mtd0` (`GetPublicKey` `0x1bbfc` → `load_pubkey_from_BSP` `0x1be24`).
  - Signature failure returns error code **`7`** (displayed as `"Signature Error"` by `opt/hmi`'s `VUpdatePage`).
- **HAB-compatible kernel image CRC**:
  - `CheckKernelImageValid` (`0x1af8c`) computes `CalImageCrc` over the `uImage` header's `ih_size` (`0x40 .. 0x40 + ih_size`) rather than `file_len - 0x40`, allowing the `8,520`-byte HAB CSF/IVT trailer appended to `kernelImage` to pass CRC validation.
- **Removed debug / legacy update paths**:
  - Removes `OnUSBUpdate`'s debug script execution hook (`stdplus_debug_script.YFVE`) and `std_plus-6.5inch-*BT_Firmware.tar` special case.
  - Removes `ReadVipImage()`'s legacy 4-byte checksum routine.

#### 2. `opt/diag_eol` (`+3,720` B `.text`)
Reference `5624` decompilation: [docs/decompiled/5624/services/diag_eol/README.md](decompiled/5624/services/diag_eol/README.md).
- Expands `DIAG_EOL_ITEM_SYSTEM` (`780 0xa544`) with four factory security provisioning subcommands:
  - **`0x05` / `0x06` (`EOL_CMD_SYSTEM_WR_SECURITY_CODE` / `EOL_CMD_SYSTEM_READ_SECURITY_CODE`)**: Persists an 8-byte security code to PSS Block `100` (`0x64`) and updates the Linux `root` password via `diag_eol_secririty_code_write` (`0x96a0`), which hex-encodes the 8 bytes (`0x95fc`), remounts `/` read-write, writes `root:<16-hex-chars>` to `/pss1/config/password.txt`, executes `chpasswd -s`, removes the temporary file, and remounts `/` read-only. This explains the stripped `/etc/shadow` hash found in the dump.
  - **`0x1d` (`DOWNLOAD_PUBLIC_KEY`)**: Writes a 32-byte HAB SRK hash into PSS Block `101` (`0x65`).
  - **`0x1e` (`READ_PUBLIC_KEY`)**: Reads Block `101`, writes byte-swapped 32-bit words to `/sys/fsl_otp/HW_OCOTP_SRK0..7` (`0x98f4`), and sets an armed flag (`DAT_00015f2e = 1`).
  - **`0x1f` (`BURN_PUBLIC_KEY`)**: Requires the armed flag, writes `0x00040000` to `/sys/fsl_otp/HW_OCOTP_MEM0` and blows `/sys/fsl_otp/HW_OCOTP_CFG5` (`0x9be0`), then syncs and reboots. *(Stock bug: logs `"sleep 3s"` while calling `usleep(1000)` three times = `3 ms`.)*

#### 3. `opt/pss` (`+0` B `.text`, `+192` B `.rodata`)
Reference `5624` decompilation: [docs/decompiled/5624/services/pss/README.md](decompiled/5624/services/pss/README.md).
- Identical executable logic; expands the static PSS block descriptor table from **`100` (`0x64`)** to **`102` (`0x66`)** dual-partition (`type=2`) entries to back `opt/diag_eol`:
  - Block `100` (`0x64`): `V_EOL_PSS_SECURITY_CODE_PASSWORD.pss` (16 B on disk: 8 B header + 8 B payload).
  - Block `101` (`0x65`): `V_EOL_PSS_PUBLIC_KEY_BLK.pss` (40 B on disk: 8 B header + 32 B SRK hash).

---

### 4.2 Smartphone Projection, Bluetooth, Media & Audio

#### 4. `opt/carplay_App` (`+1,176` B `.text`)
Reference `5624` decompilation: [docs/decompiled/5624/services/carplay_App/README.md](decompiled/5624/services/carplay_App/README.md).
- **Nested video screen-borrow reference counting**: Introduces `i_ScreenBorrowCmdNestCnt` (max depth `5`, reset to `-1` on `CMD_HMI_EXIT_CARPLAY`, `CMD_CPS_RELEASE_VIDEO[_SUCCESS]`, and `nResetVars`) with a periodic watchdog `CPA_StartCheckingBorrowNestedTimer` / `CPA_StopCheckingBorrowNestedTimer`. When timer bit `0x40` fires in the main loop (`0x9928`) while `i_ScreenBorrowCmdNestCnt > 0`, it injects state-machine event `0x21`. Replaces `5624`'s `start_source_off_timer` / `stop_source_off_timer` (`wait source off time out`).
- **Bluetooth readiness VMF handler**: Adds `HandleVmfMsgFromBT` (`0x14490`), translating incoming BT VMF sub-messages `1 → CPA_EVENT_BT_READY_SIRI (0x4a)` and `2 → CPA_EVENT_BT_READY_PHONE (0x4b)`.
- **Playback guards**: Adds `"ignore CMD_HMI_PAUSE cmd while media not playing yet"` and a NULL-pointer guard on `vp_data`.

#### 5. `opt/bt` (`−2,064` B `.text`) & 6. `opt/media` (`+516` B `.text`)
Reference `5624` decompilations: [docs/decompiled/5624/services/bt/README.md](decompiled/5624/services/bt/README.md), [docs/decompiled/5624/services/media/README.md](decompiled/5624/services/media/README.md).
- **Paired USB iPod / Bluetooth A2DP conflict arbitration (present in `780`, absent in `5624`)**:
  - In `opt/media`, `media_app_vmf_message_send_to_spp` is replaced by `media_app_send_msg_to_BT` (`0x2dac0`) → `media_app_vmf_message_send_to_BT` (`0x3bb2c`), and `media_app_spt_send_msg_build` (`0x162ac`, bounds widened `< 0x37 → < 0x38`) adds message ID **`0x37`** to send the connected USB iPod device name (and **`0x17`** on removal) over VMF `(0x24, 0x02)` to `opt/bt`.
  - In `opt/bt`, `tm_fun_core_media_name_info` (`0x454c4`, stores UTF-8 `type == 0x34` iPod name up to 128 B) and `tm_fun_core_media_remove` (`"Ipod removed"`) track the active USB iPod. In `ReceiveAlpsAuthoriseInd` (`0x31034`), if an incoming BT A2DP profile authorization (`uVar1 & 8`) matches the connected USB iPod's name, `opt/bt` rejects A2DP (`"Same device with Ipod"`) to prevent simultaneous USB/BT audio contention.
- **Additional `opt/bt` fixes in `780`**:
  - `AbstractDeviceExistDeviceAuthorise` rejects reconnects when `PowerFlag == 1` and the device is actively disconnecting (`state == 4 && enOperation == 2`).
  - `pbdl_ProcessBecameEnd` (`0x65b18`) checks combined completion (`local_complete && sim_complete && total <= 1000`).
  - Adds ALPS health-monitor query `tm_fun_core_get_healthMonitor_moduleState` (`0x3b5d0`) and AVRCP root-browse `followmodeflag`/`chgroot` guard in `tm_bt_cmd_audio_root_brw` (`0x20704`).
- **Feature present in `5624` (`8.8.9.1`) but absent in `780` (`8.5.9.7`)**:
  - `opt/bt` in `780` lacks `5624`'s **Voicemail / MailBox** feature (12 functions absent, including `SetMailBox`/`GetMailBox`, `TM_BT_SET_MAILBOX_CMD_MSGID`, and the `mailbox varchar(32)` column in `pb.db`).

#### 7. `opt/audio_proc` (`+284` B `.text`)
Reference `5624` decompilation: [docs/decompiled/5624/services/audio_proc/README.md](decompiled/5624/services/audio_proc/README.md).
- **MirrorLink ALSA/DSP teardown synchronization**:
  - `audio_vaf_mlink_thread` (`0xb58c`) notifies `0x235dc(0xc, 1)` (`"ML close ALSA!!!"`) when closing MirrorLink ALSA playback.
  - A new 2-bit flag coordinator (`0x1d388`, helpers `0x15284`/`0x1529c`/`0x152ac` on `DAT_0003bfb5`) requires **both** `am_evaluate_vmf_msg_source_service` (`0x1d3b4`, bit 1) and `audio_channel_recv_dsp_source_resp` (`0x1457c`, bit 2) to complete before dispatching `0x1c908(0, 8, 0)` during a MirrorLink (`0xc`) → source `8` transition.

#### 8. `opt/carlife_daemon` (`+344` B `.text`)
Reference `5624` decompilation: [docs/decompiled/5624/services/carlife_daemon/README.md](decompiled/5624/services/carlife_daemon/README.md).
- `start_media_playback` (`0x38cfc`, `Audio/src/media_alsa_api.cpp`) drains any residual buffered audio frames from the `std::deque` music queue on playback stop (`"Music audio buffer is not empty. Empty the buffer!!"`).
- `CTranRecvPackageProcess::packageHeadAnalysis` (`0x156b0`) resets `param_1[7] = 0` when encountering an unknown MD message type and formats the log in hex (`%x`) instead of decimal (`%d`).

#### 9. `opt/md_adapter` (`+408` B `.text`)
- Built from `STDPLUS_6.5INCH_HOST_CR222_Generate` (per embedded DWARF paths).
- Adds a `strlen(argv[2]) <= 29` bounds check (`"devPath not match the size !!!"`) in `main` (`0x939c`) before copying into the 30-byte `devPath` buffer.
- Adds NULL-pointer checks on `usb_interface`, `settings`, and `endpoint` inside `CAoapDev::populateUSBEndpointAddresses` (`0xa260`).

#### 11. `opt/AndroidAuto/bin/AAP_Application` (`+52` B `.text`) & 12. `opt/AndroidAuto/bin/aap_service` (`−256` B `.text`)
Reference `5624` decompilation: [docs/decompiled/5624/services/AAP_Application/README.md](decompiled/5624/services/AAP_Application/README.md).
- Out of `5,907` `.ARM.exidx` functions in `aap_service` and `198` in `AAP_Application`, only one paired behavioral change exists — a **newer `5624` fix absent in `780`**:
  - In `5624`, `aap_service` (`0x1436a0` → `0x165aec` reading `*(uint8_t*)(obj + 0x804)`) detects USB/protocol framing errors, logs `"Request stop due to Framing error"`, tears down the session (`0x156c0c`), and `AAP_Application::aap_session_status_cb` (`5624 0xcca8`) handles both `local_b0 == 0x10f` and `local_b0 == 0x113`.
  - In `780`, `aap_service` (`0x156bdc`) omits the framing-error check and `AAP_Application::aap_session_status_cb` (`780 0xcce0`) checks only `if (local_b0 == 0x10f)`.

---

### 4.3 MirrorLink Stack (`AllGoMLC 7.5.3 → 7.5.7`)

#### 15. `opt/lib/mirrorlink/libmlc_iface.so` (`+24,984` B `.text`)
Reference `5624` decompilation: [docs/decompiled/5624/services/libmlc_iface/README.md](decompiled/5624/services/libmlc_iface/README.md).
- Upgraded from **`AllGoMLC:1.1:7.5.3`** (`5624`) to **`AllGoMLC:1.1:7.5.7`** (`780`).
- Adds **6 new exported functions**: `ales_osal_http_download_file_full`, `process_downloaded_data`, `devmgr_timer_callback_func`, `mlc_devmgr_send_wfd_device_available`, `mlc_wfd_discover_devices`, and `mlc_wfd_listen_devices`.
- Major internal updates:
  - `audmgr_rtp_client_thread_func` (+62 decompiled lines): enhanced RTP audio silence gating and recovery.
  - `devmgr_get_app_list_from_device` (+101 lines) plus certified/notification XML parsers: updated app-list HTTP download and XML handling.
  - Adds fine-grained step-timing trace logging across connection establishment.

#### 16. `opt/lib/mirrorlink/libaupnp.so` (`+796` B `.text`) & 17. `opt/lib/mirrorlink/libupnp.so` (`+924` B `.text`)
- `libaupnp.so` (AllGo `7.5.7` companion): suppresses M-SEARCH failure when a device is already marked available (`0x11e64`), logs `ML DEVICE AVAILABLE` (`0x14778`), invokes `aupnp_uninitialize` on the error path (`0x15c5c`), and NULL-checks the device-info pointer (`0x165ec`).
- `libupnp.so`: `sock_read_write` (`0x23144`) explicitly treats `numBytes == 0` (graceful peer disconnect) as error `-0xd0` (`"numBytes is 0"`).

---

### 4.4 Vehicle Data, mDNS & Trivial Diffs

#### 10. `opt/exlap` (`0` B delta — **exact 1-byte diff**)
Reference `5624` decompilation: [docs/decompiled/5624/services/exlap/README.md](decompiled/5624/services/exlap/README.md).
- Exactly one instruction byte differs across the entire `751,740`-byte binary: at **`VA 0x1c0b4`** inside `exlap_cardata_hmi_msg_handler` (`FUN_0001bff4`), the upper-bound comparison for HMI view-option indices is `cmp r12, #0x91` (`uVar3 < 0x92`) in `5624` vs. `cmp r12, #0x90` (`uVar3 < 0x91`) in `780`.

#### 13. `opt/mdnsd` (`−4` B total, `0` B `.text`)
- **100% byte-identical to `5606_skoda`**: `CP_MDNS_R544_R2-3-gcdc9_STDPLUS_R5` (built Sep 30, 2016) vs. `5624`'s `CP_MDNS_R544_R2-4-g526e7_STDPLUS_R5` (built Dec 14, 2016).

#### 14. `opt/iperf` (`0` B delta — 4 bytes differ)
- Executable `.text` is 100% identical; only `.hash` and `.dynsym[86]` differ (`st_name = 91` `"__gxx_personality_v0"` in `5624` vs. `st_name = 90` `""` in `780` — a benign linker artifact).

---

## 5. `opt/hmi` Deep Dive: Official `CR222 8.5.9.7` Changes vs. Aftermarket German Overlay

### ELF Layout Comparison

| Section / Metric | `5624_vw/opt/hmi` | RCD 780 `opt/hmi` | Delta |
|---|---:|---:|---:|
| File size | 9,832,364 B | 9,874,228 B | +41,864 B |
| Entry point | `0x00023468` | `0x00023468` | identical |
| `.text` VA / size | `0x00010e58` / `0x003ba1d0` | `0x00010e58` / `0x003bf488` | +21,176 B |
| `.rodata` VA / size | `0x003cb030` / `0x0055e7bc` | `0x003d02e8` / `0x0056399c` | +20,960 B |
| `.ARM.extab` size | `0x00023ea0` | `0x00023d74` | −300 B |
| `.ARM.exidx` entries | 8,994 (`0x11910` B) | 9,010 (`0x11990` B) | +16 functions |
| `.data` VA / size | `0x00967970` / `0x0000885c` | `0x00971cf8` / `0x0000885c` | 0 B |
| `.bss` size | `0x00504b94` | `0x00506960` | +7,628 B |
| Compressed string table base | `file 0x3c3680` (`VA 0x3cb680`) | `file 0x3c8938` (`VA 0x3d0938`) | max id `0x20fcb → 0x21156` |
| UTF-32 engineering strings | `file 0x89cc00` (`VA 0x8a4c00`) | `file 0x8a5534` (`VA 0x8ad534`) | shifted by `.rodata` growth |
| Unique `*_ovg.bin` names in binary | 2,806 | 2,904 | +98 textures |

### 5.1 Official Visteon `CR222 8.5.9.7` HMI Code & UI Differences

1. **Software Update Signature Error**:
   - `VUpdatePage` (`780 0x1ee4cc`, `0x1ee4f8`, `0x1ee7d0`, `0x1ee8b8`) adds handling for `cVar1 == 6` (corresponding to `opt/sysupdate` error code `7`) and displays **`"Signature Error"`**.
2. **Rear Traffic Alert (RTA)**:
   - Adds `VBAPProxy::onSetRTAActive` (`780 0xb0c34`), a toggle button (`pBtnRTAAtive` / `OnBtnRTAActiveClicked`) in `VCarPdcSetupPage` (`780 0x37b8f0`), and left/right RTA directional warning overlays in `vcar_ops` (`780 0x14c5c8`).
3. **Driving Mode Selection (FPA) Enhancements**:
   - `VCarFpaPage` adds **Snow mode** (`mBtnSnow` / `OnSnowClicked` at `780 0x29ce94`), Race mode profile artwork, dynamic popup layout switching (`SwitchFPAPopType` at `780 0x29f514`), and Climate-on-FPA coordination (`SetShowClimateOnFPAFlag` `0xbf70c` / `GetShowClimateOnFPAFlag` `0xbf778`).
4. **Touchscreen Climate Temperature Controls**:
   - `vClimate_main` (`780 0x279f88`) and `VBAPProxy` (`780 0xb98d8`) add direct touchscreen Driver and Co-driver temperature +/- buttons (`OnDriverTemIncreaseBtnClick`, `OnDriverTemDecreaseBtnClick`, `OnCoDriverTemIncreaseBtnClick`, `OnCoDriverTemDecreaseBtnClick` → `OnSetBAPClimateDriverTemperature` / `OnSetBAPClimateCoDriverTemperature`), and `vClimate_setup` (`780 0x292da4`) is expanded.
5. **Composite Faceplate Hardkey**:
   - `VIPCLNotifyFilter` (`780 0x43a2c`) registers an additional composite button press `{5, 1}` (**PHONE + RADIO**, mode `9`).
6. **MQB / `CR222` Car Silhouettes & App-Connect Info Split**:
   - `VPowerProxy::mqbOpsCarType` (`780 0x5b69c`) adds project/region selection branches for `VHCL_VW3165CS_KL`, `VW4211CS_K`, `VW3130CS_K`, `VW2700CS_K1`, and `VW2160CS_K`.
   - `vappconnectinfo.cpp` (`780 0x34a618`) splits App-Connect info page graphics between GP VW (`_gp_vw`) and ROW (`_row`) variants.
7. **Miscellaneous `8.5.9.7` HMI Updates**:
   - `VSoundSetupMain::UpdateEQDisplayStatus` (`780 0x1b6fc8`), SMS display timeout timer (`m_displaySmsTimer` at `780 0xefe98`), BT contacts download completion check (`780 0xcbf98`), MirrorLink USB VID logging (`780 0xdeb94`), `VMediaBrwPage::MSG_MEDIA_APP_PLAY_EVENT_RET` (`780 0x119790`), Start-Stop clutch pedal prompt (`780 0x376470`), and compressed string decoder `FUN_00023650` bounds widened from `0x20fcc / 0x2cf` (`5624`) to `0x21156 / 0x2e1` (`780`).
8. **Newer `5624` (`8.8.9.1`) HMI Features Absent from `780` (`8.5.9.7`)**:
   - `VBTProxy::OnBTSetMailBoxCmd` (`5624 0xc0a10`, paired with `opt/bt` MailBox).
   - `VDiagProxy::DiagThresholdHighNotify` `case 0x1b` (`5624 0x1fe10c`).
   - Android Auto speed-limit coding parameters `uc_aa_limit_speed` / `uc_aa_limit_hysteresis` (at the corresponding slot `780 0x38ab54`, `780` still exposes `uc_shutoff_bt_bonding` / `uc_shutoff_bt_bonding_hysteresis`).

### 5.2 `opt/gresfiles/` Asset Differences

- **Totals**: `5624` has `2,859` files vs. `3,001` in RCD 780 (`2,596` identical, `195` changed, `68` only in `5624`, `210` only in `780`).
- **Official `CR222 8.5.9.7` asset additions**: PureAir cabin air animations (`vw316_5cs_kl`, `vw330_4cs_k`, `vw331_4cs_k`), FPA Snow/Race/profile/headline graphics, OPS Rear Traffic Alert (RTA) left/right warning bars, top-row driver/co-driver climate temperature buttons, and split App-Connect info screens (`_gp_vw` / `_row`).
- **Aftermarket overlay asset additions**: 4 speller language toggle icons (`img_togglelanguage_{cn,de,me,tr}_ovg.bin`) and the modified `FZHT_GB18030.ttf`.

### 5.3 Aftermarket German + `5814` Localization Overlay (Forensic Breakdown)

#### Files Modified by the Overlay (`2016-04-24` Uninitialized-RTC Epoch)

While all unmodified `8.5.9.7` binaries and assets are stamped `2018-11-23 18:03–18:04 UTC`, **9 application/asset files** carry timestamps from a single 14-minute patching session when the Linux system clock was at `2016-04-24`:

| Timestamp (UTC) | Size (B) | File | Role in Overlay |
|---|---:|---|---|
| `2016-04-24 10:05:13` | 386 | `opt/stdplus.ini` | Spoofs `CUSTM_PQ=5814`, `CUSTM_MQB=5814`, `AP_VER=5.4.9.2`, `BT_VER=C370` (mirrored to `pss1/stdplus.ini` at `10:05:26`) |
| `2016-04-24 10:05:49` | 743 | `opt/gresfiles/img_togglelanguage_tr_ovg.bin` | Added Turkish keyboard toggle icon |
| `2016-04-24 10:08:22` | 134,088 | `opt/gresfiles/VWThesis MQB Regular.ttf` | Re-copied during overlay (byte-identical to `5624`) |
| `2016-04-24 10:08:22` | 146,696 | `opt/gresfiles/VWThesis MQB Light.ttf` | Re-copied during overlay (byte-identical to `5624`) |
| `2016-04-24 10:08:34` | 461 | `opt/gresfiles/img_togglelanguage_de_ovg.bin` | Added German keyboard toggle icon |
| `2016-04-24 10:08:46` | 878 | `opt/gresfiles/img_togglelanguage_cn_ovg.bin` | Added Chinese keyboard toggle icon |
| `2016-04-24 10:16:00` | 9,874,228 | `opt/hmi` | In-place `.rodata` hex-patch for German strings + button slot reroute |
| `2016-04-24 10:16:08` | 12,016,216 | `opt/gresfiles/FZHT_GB18030.ttf` | FontCreator 11.5 (`November 28, 2019`, `+110,504` B) with 41 whole-word `U+FFxx` German glyphs |
| `2016-04-24 10:19:10` | 461 | `opt/gresfiles/img_togglelanguage_me_ovg.bin` | Added speller toggle icon |

#### How the In-Place German Patch Works (Contrast with `5606_skoda`)

Whereas the community German patch in [5606_skoda](diff-5624-vs-5606.md) overwrote Slot 2 (`pt_BR`) in-place, the RCD 780 overlay targets **Slot 1 (`zh_CN`)** and uses two clever tricks to overcome byte-length constraints without recompiling `opt/hmi`:

1. **Qt Signal/Slot Hex-Patch (`pBtnLangBRAZIL` → `OnBtnLangChineseClicked`)**:
   - In the compressed string table at `file 0x3c8b1c` (`VA 0x3d0b1c`), the `Português (BR)` menu entry (`0x1e3`) is hex-edited to `Deutsch (DE)\x01)`.
   - In `.rodata` at `file 0x41e130` (`VA 0x426130`), the Qt `SLOT(...)` string wired to `pBtnLangBRAZIL` inside `VSystemSetupLanguageSelectPage` (`780 0x16b790`) was hex-edited in-place from:
     - `1OnBtnLangBRAZILClicked(Item *, int32_t, int32_t, int32_t)` (57 chars)
     - to **`1OnBtnLangChineseClicked(Item *, int32_t, int32_t, int32_t)`** (58 chars, consuming 1 trailing alignment NUL byte; identical to the stock Chinese slot string at `VA 0x42620c`).
   - Tapping **"Deutsch (DE)"** in System Setup therefore switches the active language index to **Slot 1 (`zh_CN`)** instead of Slot 2 (`pt_BR`).
2. **Slot 1 + Slot 2 Overflow & Custom `U+FFxx` Whole-Word Glyphs in `FZHT_GB18030.ttf`**:
   - Because UTF-8 Chinese strings in Slot 1 are typically much shorter than their German equivalents, the modder writes German text across Slot 1 and lets longer translations **overflow into Slot 2 (`pt_BR`)**, leaving Slots 3 (`ru_RU`) and 4 (`es_MX`) at their exact compiled byte offsets.
   - Where even the combined Slot 1 + Slot 2 budget was too short for plain ASCII German, the modder opened `FZHT_GB18030.ttf` in **FontCreator 11.5.0.2427** (`Version 1.00;November 28, 2019`) and added **41 custom Unicode BMP cmap entries** (`28,522 → 28,563` entries: `0x0000`, `0x001d`, `U+FF5F..U+FF69`, `U+FF7A..U+FF7D`, `U+FFE6..U+FFFD`) whose single glyph outlines render **entire German words** in 3 UTF-8 bytes — e.g.:
     - `U+FFF9` → `"Aus"`
     - `U+FF68` → `"Ein"`
     - `U+FFF7` → `"Abbrechen"`
     - `U+FFFA` → `"Ergebnisse"`
     - `U+FFF8` → `"Bluetooth"`
     - `U+FFE9` → `"Bluetooth-Geräte"`
     - `U+FFE6` → zero-width padding glyph
   - These custom `U+FFxx` codepoints appear **`253` times** inside RCD 780's `opt/hmi` string table (compared to just `3` occurrences in stock `5624_vw`).
