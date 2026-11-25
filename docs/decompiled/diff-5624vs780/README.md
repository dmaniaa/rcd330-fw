[← Decompiled Firmware Archive](../README.md) · [5624 Baseline Archive](../5624/README.md) · [Full Behavioral Diff Report](../../diff-5624-vs-rcd780.md)

# Decompiled Diff Archive: `5624_vw` vs. RCD 780 (`8.5.9.7` CR222 + `5814` DE Overlay)

Complete Ghidra 12.2 `decompile_all` exports for **all `12` differing core binaries** (a strict subset of the `32` core binaries tracked in [`docs/decompiled/5624/`](../5624/README.md)) from the RCD 780 dump (`std_plus_pss-6.5inch_181124-8.5.9.7_VW_MQB_CR222_sig.tar` + post-Nov 2019 German/`5814` overlay) — **`21,042` decompiled `.c` function files total** (`1` failure: `_INIT_3` @ `0x11658` in `opt/hmi`, matching `5624_vw`).

All other core services (`opt/vmf`, `opt/vgw`, `opt/pwr_agent`, `opt/hal_init`, `opt/diag`, `opt/camera`, `opt/screenshot`, `opt/connect_mgr`, `opt/carplayd`, `opt/iap2d`, `opt/mfid`, `libHAL`, `libOSAL`, `libvmf_*`, `libCarPlay*`) are **100% byte-identical** to `5624_vw` (`opt/tools/vmf_{send,rcv}` are absent in the dump, and `vipImage.bin` is not present in the Linux rootfs dump). Non-core third-party SDK/helper binaries (`aap_service`, `md_adapter`, `mdnsd`, `iperf`, `libaupnp.so`, `libupnp.so`) are omitted from raw decompilation archives per `AGENTS.md` §4 and documented in [docs/diff-5624-vs-rcd780.md](../../diff-5624-vs-rcd780.md).

> **Public repository note:** Raw Ghidra pseudocode (`functions/*.c`) and raw export logs (`decompile-index.json`, `decompile-journal.jsonl`) are intentionally excluded from the public GitHub mirror (`github-main`). All per-binary `README.md` summaries and [docs/diff-5624-vs-rcd780.md](../../diff-5624-vs-rcd780.md) are included.

---

## Differing Core Binary Inventory (`12` of `32` Core Binaries)

| # | Binary (RCD 780) | Size (B) | Overview | Functions | Written / Total / External (Failed) |
|---:|---|---:|---|---|---:|
| 1 | `opt/hmi` | 9,874,228 | [hmi/README.md](hmi/README.md) | [hmi/functions/](hmi/functions) | 9,606 / 10,091 / 484 (1: `_INIT_3` @ `0x11658`) |
| 2 | `opt/sysupdate` | 728,396 | [services/sysupdate/README.md](services/sysupdate/README.md) | [services/sysupdate/functions/](services/sysupdate/functions) | 2,310 / 2,699 / 389 (0) |
| 3 | `opt/diag_eol` | 24,956 | [services/diag_eol/README.md](services/diag_eol/README.md) | [services/diag_eol/functions/](services/diag_eol/functions) | 106 / 137 / 31 (0) |
| 4 | `opt/pss` | 54,156 | [services/pss/README.md](services/pss/README.md) | [services/pss/functions/](services/pss/functions) | 140 / 183 / 43 (0) |
| 5 | `opt/carplay_App` | 120,060 | [services/carplay_App/README.md](services/carplay_App/README.md) | [services/carplay_App/functions/](services/carplay_App/functions) | 228 / 305 / 77 (0) |
| 6 | `opt/bt` | 678,596 | [services/bt/README.md](services/bt/README.md) | [services/bt/functions/](services/bt/functions) | 1,524 / 1,680 / 156 (0) |
| 7 | `opt/media` | 384,008 | [services/media/README.md](services/media/README.md) | [services/media/functions/](services/media/functions) | 1,157 / 1,305 / 148 (0) |
| 8 | `opt/audio_proc` | 168,940 | [services/audio_proc/README.md](services/audio_proc/README.md) | [services/audio_proc/functions/](services/audio_proc/functions) | 698 / 838 / 140 (0) |
| 9 | `opt/carlife_daemon` | 286,236 | [services/carlife_daemon/README.md](services/carlife_daemon/README.md) | [services/carlife_daemon/functions/](services/carlife_daemon/functions) | 1,100 / 1,298 / 198 (0) |
| 10 | `opt/exlap` | 751,740 | [services/exlap/README.md](services/exlap/README.md) | [services/exlap/functions/](services/exlap/functions) | 1,977 / 2,212 / 235 (0) |
| 11 | `opt/AndroidAuto/bin/AAP_Application` | 154,648 | [services/AAP_Application/README.md](services/AAP_Application/README.md) | [services/AAP_Application/functions/](services/AAP_Application/functions) | 198 / 264 / 66 (0) |
| 12 | `opt/lib/mirrorlink/libmlc_iface.so` | 1,307,116 | [services/libmlc_iface/README.md](services/libmlc_iface/README.md) | [services/libmlc_iface/functions/](services/libmlc_iface/functions) | 1,998 / 2,235 / 237 (0) |
| **Total** | **12 Core Binaries** | **14,533,080** | — | `docs/decompiled/diff-5624vs780/` | **21,042 / 23,247 / 2,204 (1)** |
