[← Decompiled Firmware Archive](../README.md) · [5624 Baseline Archive](../5624/README.md) · [Full Behavioral Diff Report](../../diff-5624-vs-5606.md)

# Decompiled Diff Archive: `5624_vw` vs. `5606_skoda` (`7.1.2.2` GP + `DE_CV` Overlay)

Complete Ghidra 12.2 decompilation exports for **all `20` differing core binaries** (`19` ARMv7-A ELFs + Renesas RH850 `vipImage.bin` — a strict subset of the `32` core binaries tracked in [`docs/decompiled/5624/`](../5624/README.md)) from `extracted-fw/5606_skoda/` (`std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar`) — **`23,776` decompiled `.c` function files total** (`1` failure: `_INIT_3` @ `0x11680` in `opt/hmi`, matching `5624_vw`).

The remaining `12` core binaries tracked in `5624/` (`opt/vmf`, `opt/hal_init`, `opt/lib/libHAL.so.1.0.0`, `opt/lib/libvmf_*`, `opt/tools/vmf_*`, `opt/camera`, `opt/diag`, `opt/diag_eol`, `opt/screenshot`, `opt/lib/mirrorlink/libmlc_iface.so`) are **100% byte-identical** to `5624_vw`. Non-core third-party SDK/helper binaries (`aap_service`, `libAirPlay*.so`, `libCarPlay*.so`, `libiap2.so`, `libcidb_sdk.so`, `md_adapter`, `mdnsd`, `librace_iface.so`, etc.) are intentionally omitted from raw decompilation archives per `AGENTS.md` §4 and documented in [docs/diff-5624-vs-5606.md](../../diff-5624-vs-5606.md).

> **Public repository note:** Raw Ghidra pseudocode (`functions/*.c`) and raw export logs (`decompile-index.json`, `decompile-journal.jsonl`) are intentionally excluded from the public GitHub mirror (`github-main`). All per-binary `README.md` summaries and [docs/diff-5624-vs-5606.md](../../diff-5624-vs-5606.md) are included.

---

## Differing Core Binary Inventory (`20` of `32` Core Binaries)

| # | Binary (`5606_skoda`) | Size (B) | Overview | Functions | Written / Total / External (Failed) |
|---:|---|---:|---|---|---:|
| 1 | `opt/hmi` | 10,282,460 | [hmi/README.md](hmi/README.md) | [hmi/functions/](hmi/functions) | 9,424 / 9,910 / 485 (1: `_INIT_3` @ `0x11680`) |
| 2 | `opt/vgw` | 23,594 | [vgw/README.md](vgw/README.md) | [vgw/pseudocode/functions/](vgw/pseudocode/functions) | 117 / 146 / 29 (0) |
| 3 | `vipImage.bin` (Renesas RH850 VIP MCU) | 1,048,588 | [vip/README.md](vip/README.md) | [vip/functions/](vip/functions) | 2,814 / 2,814 / 0 (0) |
| 4 | `opt/lib/libOSAL.so.1.0.0` | 55,656 | [hal_osal/libOSAL/README.md](hal_osal/libOSAL/README.md) | [hal_osal/libOSAL/functions/](hal_osal/libOSAL/functions) | 396 / 523 / 127 (0) |
| 5 | `opt/pwr_agent` | 48,824 | [services/pwr_agent/README.md](services/pwr_agent/README.md) | [services/pwr_agent/functions/](services/pwr_agent/functions) | 227 / 303 / 76 (0) |
| 6 | `opt/pss` | 53,824 | [services/pss/README.md](services/pss/README.md) | [services/pss/functions/](services/pss/functions) | 140 / 183 / 43 (0) |
| 7 | `opt/audio_proc` | 168,404 | [services/audio_proc/README.md](services/audio_proc/README.md) | [services/audio_proc/functions/](services/audio_proc/functions) | 690 / 830 / 140 (0) |
| 8 | `opt/media` | 382,284 | [services/media/README.md](services/media/README.md) | [services/media/functions/](services/media/functions) | 1,152 / 1,299 / 147 (0) |
| 9 | `opt/bt` | 670,404 | [services/bt/README.md](services/bt/README.md) | [services/bt/functions/](services/bt/functions) | 1,515 / 1,671 / 156 (0) |
| 10 | `opt/sysupdate` | 716,016 | [services/sysupdate/README.md](services/sysupdate/README.md) | [services/sysupdate/functions/](services/sysupdate/functions) | 2,279 / 2,649 / 370 (0) |
| 11 | `opt/exlap` | 750,828 | [services/exlap/README.md](services/exlap/README.md) | [services/exlap/functions/](services/exlap/functions) | 1,975 / 2,210 / 235 (0) |
| 12 | `opt/connect_mgr` | 53,028 | [services/connect_mgr/README.md](services/connect_mgr/README.md) | [services/connect_mgr/functions/](services/connect_mgr/functions) | 230 / 304 / 74 (0) |
| 13 | `opt/carplay_App` | 114,084 | [services/carplay_App/README.md](services/carplay_App/README.md) | [services/carplay_App/functions/](services/carplay_App/functions) | 227 / 304 / 77 (0) |
| 14 | `opt/carplayd` | 84,820 | [services/carplayd/README.md](services/carplayd/README.md) | [services/carplayd/functions/](services/carplayd/functions) | 398 / 550 / 152 (0) |
| 15 | `opt/iap2d` | 158,760 | [services/iap2d/README.md](services/iap2d/README.md) | [services/iap2d/functions/](services/iap2d/functions) | 469 / 546 / 77 (0) |
| 16 | `opt/mfid` | 28,816 | [services/mfid/README.md](services/mfid/README.md) | [services/mfid/functions/](services/mfid/functions) | 247 / 322 / 75 (0) |
| 17 | `opt/lib/libCarPlayAudio.so` | 16,976 | [services/libCarPlayAudio/README.md](services/libCarPlayAudio/README.md) | [services/libCarPlayAudio/functions/](services/libCarPlayAudio/functions) | 132 / 189 / 57 (0) |
| 18 | `opt/lib/libCarPlayPlatformSystem.so` | 6,888 | [services/libCarPlayPlatformSystem/README.md](services/libCarPlayPlatformSystem/README.md) | [services/libCarPlayPlatformSystem/functions/](services/libCarPlayPlatformSystem/functions) | 71 / 97 / 26 (0) |
| 19 | `opt/AndroidAuto/bin/AAP_Application` | 147,660 | [services/AAP_Application/README.md](services/AAP_Application/README.md) | [services/AAP_Application/functions/](services/AAP_Application/functions) | 175 / 230 / 55 (0) |
| 20 | `opt/carlife_daemon` | 285,924 | [services/carlife_daemon/README.md](services/carlife_daemon/README.md) | [services/carlife_daemon/functions/](services/carlife_daemon/functions) | 1,098 / 1,295 / 197 (0) |
| **Total** | **20 Core Binaries** | **15,097,838** | — | `docs/decompiled/diff-5624vs5606/` | **23,776 / 26,375 / 2,598 (1)** |
