[← Decompiled Firmware Archive](../README.md) · [5606 Diff Archive](../diff-5624vs5606/README.md) · [RCD 780 Diff Archive](../diff-5624vs780/README.md)

# Decompiled RCD 330 Firmware Archive (`5624_vw`)

Complete Ghidra 12.2 decompilation archive for **all 31 core ARMv7-A Linux ELF binaries and libraries** (`rea` `decompile_all`, `24,598` `.c` files) **plus the Renesas RH850 VIP MCU firmware** (`vipImage.bin`, `2,824` `.c` files) from `extracted-fw/5624_vw/` — **`27,422` decompiled `.c` function files total**.

Every binary subdirectory has its own `README.md` summarizing its role, IPC/hardware interfaces, recovered source files, and key verified functions, alongside its `decompile-index.json`, `decompile-journal.jsonl`, and `functions/<addr>.c` exports.

> **Public repository note:** Raw Ghidra pseudocode (`functions/*.c`) and raw export logs (`decompile-index.json`, `decompile-journal.jsonl`) are intentionally excluded from the public GitHub mirror (`github-main`). All 32 per-binary `README.md` summaries, `hmi_symbols_5624.json`, texture catalogs, written specs (`docs/*.md`), and reproducible Ghidra scripts (`tools/ghidra/`) are included; `functions/*.c` links resolve locally when populated from your own firmware image.

---

## Complete Binary Inventory (`32` Binaries)

| # | Binary (`5624_vw`) | Size (B) | Overview | Functions | Written / Total / External (Failed) |
|---:|---|---:|---|---|---:|
| 1 | `opt/hmi` | 9,832,364 | [hmi/README.md](hmi/README.md) | [hmi/functions/](hmi/functions) | 9,580 / 10,065 / 484 (1: `_INIT_3` @ `0x11658`) |
| 2 | `opt/vgw` | 23,594 | [vgw/README.md](vgw/README.md) | [vgw/pseudocode/functions/](vgw/pseudocode/functions) | 117 / 146 / 29 (0) |
| 3 | `opt/lib/libHAL.so.1.0.0` | 34,836 | [hal_osal/libHAL/README.md](hal_osal/libHAL/README.md) | [hal_osal/libHAL/functions/](hal_osal/libHAL/functions) | 187 / 237 / 50 (0) |
| 4 | `opt/lib/libOSAL.so.1.0.0` | 55,656 | [hal_osal/libOSAL/README.md](hal_osal/libOSAL/README.md) | [hal_osal/libOSAL/functions/](hal_osal/libOSAL/functions) | 396 / 523 / 127 (0) |
| 5 | `opt/hal_init` | 5,731 | [hal_osal/hal_init/README.md](hal_osal/hal_init/README.md) | [hal_osal/hal_init/functions/](hal_osal/hal_init/functions) | 22 / 29 / 7 (0) |
| 6 | `opt/vmf` | 155,892 | [vmf/vmf_daemon/README.md](vmf/vmf_daemon/README.md) | [vmf/vmf_daemon/functions/](vmf/vmf_daemon/functions) | 431 / 518 / 87 (0) |
| 7 | `opt/lib/libvmf_client.so` | 57,880 | [vmf/libvmf_client/README.md](vmf/libvmf_client/README.md) | [vmf/libvmf_client/functions/](vmf/libvmf_client/functions) | 212 / 258 / 46 (0) |
| 8 | `opt/lib/libvmf_trace.so` | 27,180 | [vmf/libvmf_trace/README.md](vmf/libvmf_trace/README.md) | [vmf/libvmf_trace/functions/](vmf/libvmf_trace/functions) | 119 / 151 / 32 (0) |
| 9 | `opt/tools/vmf_send` | 7,038 | [vmf/vmf_send/README.md](vmf/vmf_send/README.md) | [vmf/vmf_send/functions/](vmf/vmf_send/functions) | 30 / 41 / 11 (0) |
| 10 | `opt/tools/vmf_rcv` | 7,555 | [vmf/vmf_rcv/README.md](vmf/vmf_rcv/README.md) | [vmf/vmf_rcv/functions/](vmf/vmf_rcv/functions) | 37 / 51 / 14 (0) |
| 11 | `opt/pwr_agent` | 49,316 | [services/pwr_agent/README.md](services/pwr_agent/README.md) | [services/pwr_agent/functions/](services/pwr_agent/functions) | 227 / 303 / 76 (0) |
| 12 | `opt/pss` | 53,964 | [services/pss/README.md](services/pss/README.md) | [services/pss/functions/](services/pss/functions) | 140 / 183 / 43 (0) |
| 13 | `opt/audio_proc` | 168,636 | [services/audio_proc/README.md](services/audio_proc/README.md) | [services/audio_proc/functions/](services/audio_proc/functions) | 693 / 833 / 140 (0) |
| 14 | `opt/media` | 384,008 | [services/media/README.md](services/media/README.md) | [services/media/functions/](services/media/functions) | 1,155 / 1,303 / 148 (0) |
| 15 | `opt/bt` | 680,932 | [services/bt/README.md](services/bt/README.md) | [services/bt/functions/](services/bt/functions) | 1,525 / 1,681 / 156 (0) |
| 16 | `opt/camera` | 14,428 | [services/camera/README.md](services/camera/README.md) | [services/camera/functions/](services/camera/functions) | 79 / 110 / 31 (0) |
| 17 | `opt/diag` | 70,328 | [services/diag/README.md](services/diag/README.md) | [services/diag/functions/](services/diag/functions) | 148 / 177 / 29 (0) |
| 18 | `opt/diag_eol` | 19,568 | [services/diag_eol/README.md](services/diag_eol/README.md) | [services/diag_eol/functions/](services/diag_eol/functions) | 85 / 108 / 23 (0) |
| 19 | `opt/sysupdate` | 724,208 | [services/sysupdate/README.md](services/sysupdate/README.md) | [services/sysupdate/functions/](services/sysupdate/functions) | 2,267 / 2,637 / 370 (0) |
| 20 | `opt/screenshot` | 55,684 | [services/screenshot/README.md](services/screenshot/README.md) | [services/screenshot/functions/](services/screenshot/functions) | 399 / 544 / 145 (0) |
| 21 | `opt/exlap` | 751,740 | [services/exlap/README.md](services/exlap/README.md) | [services/exlap/functions/](services/exlap/functions) | 1,977 / 2,212 / 235 (0) |
| 22 | `opt/connect_mgr` | 55,560 | [services/connect_mgr/README.md](services/connect_mgr/README.md) | [services/connect_mgr/functions/](services/connect_mgr/functions) | 233 / 308 / 75 (0) |
| 23 | `opt/carplay_App` | 116,508 | [services/carplay_App/README.md](services/carplay_App/README.md) | [services/carplay_App/functions/](services/carplay_App/functions) | 229 / 306 / 77 (0) |
| 24 | `opt/carplayd` | 83,340 | [services/carplayd/README.md](services/carplayd/README.md) | [services/carplayd/functions/](services/carplayd/functions) | 397 / 549 / 152 (0) |
| 25 | `opt/iap2d` | 169,152 | [services/iap2d/README.md](services/iap2d/README.md) | [services/iap2d/functions/](services/iap2d/functions) | 495 / 576 / 81 (0) |
| 26 | `opt/mfid` | 31,360 | [services/mfid/README.md](services/mfid/README.md) | [services/mfid/functions/](services/mfid/functions) | 252 / 329 / 77 (0) |
| 27 | `opt/lib/libCarPlayAudio.so` | 20,160 | [services/libCarPlayAudio/README.md](services/libCarPlayAudio/README.md) | [services/libCarPlayAudio/functions/](services/libCarPlayAudio/functions) | 150 / 212 / 62 (0) |
| 28 | `opt/lib/libCarPlayPlatformSystem.so` | 7,096 | [services/libCarPlayPlatformSystem/README.md](services/libCarPlayPlatformSystem/README.md) | [services/libCarPlayPlatformSystem/functions/](services/libCarPlayPlatformSystem/functions) | 71 / 97 / 26 (0) |
| 29 | `opt/AndroidAuto/bin/AAP_Application` | 154,596 | [services/AAP_Application/README.md](services/AAP_Application/README.md) | [services/AAP_Application/functions/](services/AAP_Application/functions) | 198 / 264 / 66 (0) |
| 30 | `opt/carlife_daemon` | 285,928 | [services/carlife_daemon/README.md](services/carlife_daemon/README.md) | [services/carlife_daemon/functions/](services/carlife_daemon/functions) | 1,099 / 1,296 / 197 (0) |
| 31 | `opt/lib/mirrorlink/libmlc_iface.so` | 1,266,543 | [services/libmlc_iface/README.md](services/libmlc_iface/README.md) | [services/libmlc_iface/functions/](services/libmlc_iface/functions) | 1,994 / 2,233 / 239 (0) |
| 32 | `vipImage.bin` (Renesas RH850 VIP MCU) | 1,048,588 | [vip/README.md](vip/README.md) | [vip/functions/](vip/functions) | 2,824 / 2,824 / 0 (0) |
| **Total** | **32 Binaries** | **16,589,969** | — | `docs/decompiled/5624/` | **27,422 / 30,652 / 3,229 (1)** |

---

## Cross-Cutting Technical Specs

- [Architecture, Boot Chain & Process Supervision](../../architecture.md)
- [`opt/hmi` Internals (Qt4 Event Loop + IGDT GLES2 + All 18 Proxies)](../../hmi-internals.md)
- [VMF IPC Protocol & `opt/vgw` SPI IPCL Routing Matrix](../../ipc-and-vgw.md)
- [VW Long Coding (`DID 0x0600`), Feature Manager & RVC/OPS Wire Spec](../../coding.md)
- [IGDT Assets (`_ovg.bin`), Scene Graphs & Multi-Language Strings](../../assets-and-localization.md)
- [Firmware Packaging (`opt/sysupdate`) & QEMU Rootfs Staging](../../firmware-packaging-and-update.md)
- [Known Firmware Bugs: Android Auto BT Call Echo (ALPS ECNR)](../../bugs/android-auto-echo-ecnr.md)
- [5624 (VW) vs. 5606 (ŠKODA `7.1.2.2` + `DE_CV`) Firmware Diff](../../diff-5624-vs-5606.md)
- [5624 (VW) vs. RCD 780 (`8.5.9.7` CR222 + `5814` DE Overlay) Firmware Diff](../../diff-5624-vs-rcd780.md)
