# Decompiled RCD 330 / RCD 780 Firmware Archive

Complete Ghidra 12.2 decompilation archives organized by firmware baseline and binary-diff delta:

| Archive | Target Firmware | Scope | Index |
|---|---|---|---|
| [`5624/`](5624/README.md) | Stock VW `5624` (`5624_vw`, `AP_VER=8.8.9.1`, PQ ROW) | All **31 core ARMv7-A ELF binaries & libraries** + **Renesas RH850 VIP MCU (`vipImage.bin`)** — **`32` binaries / `27,422` decompiled functions** | [5624/README.md](5624/README.md) |
| [`diff-5624vs5606/`](diff-5624vs5606/README.md) | ŠKODA `5606` (`5606_skoda`, `AP_VER=7.1.2.2` GP + `DE_CV` German overlay) | Strict subset of `5624/`'s 32 core binaries: **all `20` differing core binaries** (`19` ARMv7 ELFs + RH850 `vipImage.bin` = `23,776` `.c` files) | [diff-5624vs5606/README.md](diff-5624vs5606/README.md) |
| [`diff-5624vs780/`](diff-5624vs780/README.md) | RCD 780 Dump (`8.5.9.7` VW MQB `CR222` + Nov 2019 `5814` DE overlay) | Strict subset of `5624/`'s 32 core binaries: **all `12` differing core binaries** (`21,042` `.c` files; all other core services are 100% byte-identical) | [diff-5624vs780/README.md](diff-5624vs780/README.md) |

> **Public repository note:** Raw Ghidra pseudocode (`functions/*.c`) and raw export logs (`decompile-index.json`, `decompile-journal.jsonl`) under `5624/` and all `diff-*` archives are intentionally excluded from the public GitHub mirror (`github-main`) by `tools/export_public.sh`. All per-binary `README.md` summaries, `hmi_symbols_5624.json`, texture catalogs, written specs (`docs/*.md`), and reproducible Ghidra scripts (`tools/ghidra/`) are included; `functions/*.c` links resolve locally when populated from your own firmware image.

---

## Cross-Cutting Technical Specs

- [Architecture, Boot Chain & Process Supervision](../architecture.md)
- [`opt/hmi` Internals (Qt4 Event Loop + IGDT GLES2 + All 18 Proxies)](../hmi-internals.md)
- [VMF IPC Protocol & `opt/vgw` SPI IPCL Routing Matrix](../ipc-and-vgw.md)
- [VW Long Coding (`DID 0x0600`), Feature Manager & RVC/OPS Wire Spec](../coding.md)
- [IGDT Assets (`_ovg.bin`), Scene Graphs & Multi-Language Strings](../assets-and-localization.md)
- [Firmware Packaging (`opt/sysupdate`) & QEMU Rootfs Staging](../firmware-packaging-and-update.md)
- [Known Firmware Bugs: Android Auto BT Call Echo (ALPS ECNR)](../bugs/android-auto-echo-ecnr.md)
- [5624 (VW) vs. 5606 (ŠKODA `7.1.2.2` + `DE_CV`) Firmware Diff](../diff-5624-vs-5606.md)
- [5624 (VW) vs. RCD 780 (`8.5.9.7` CR222 + `5814` DE Overlay) Firmware Diff](../diff-5624-vs-rcd780.md)
