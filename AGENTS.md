# RCD 330 Project Guide for AI Agents (`AGENTS.md`)

This repository contains:
1. **The archived reverse-engineering knowledge base & full decompilation archive** (`docs/` and `resources/`) for the Noname/Visteon RCD 330 (`6RD 035 187 B/C/E/F`).
2. **The QEMU-based full-system emulator** (`qemu/` and `tools/`) that boots and runs the unmodified original retail firmware (`5624_vw`) with display (`800x480`), touchscreen, physical faceplate buttons/knobs, and simulated CAN/BAP vehicle telemetry.
3. **The recreated headunit software** (`src/hmi/` — C++ recreation of `opt/hmi` for `5624_vw`, with Phase 0–1 toolchain & IGDT renderer complete; one `src/<binary>/` directory per recreated stock binary).

> **Rule #1 — Do NOT re-decompile binaries unnecessarily.** All **31 core ARMv7 ELF binaries and libraries** (`24,598` functions, including all `9,580` functions of `opt/hmi`) **plus the Renesas RH850 VIP MCU firmware `vipImage.bin` (`2,824` functions)** from `5624_vw` have already been decompiled via Ghidra 12.2 into `docs/decompiled/5624/` (plus `20` differing core `5606_skoda` binaries in `docs/decompiled/diff-5624vs5606/` and `12` differing core RCD 780 binaries in `docs/decompiled/diff-5624vs780/`) and documented in `docs/`. Consult those files first.

---

## 1. Repository Map

| Path | Contents |
|---|---|
| `qemu/` | Custom QEMU submodule branch (`rcd330`) implementing `-M rcd330` ([qemu/hw/arm/rcd330.c](qemu/hw/arm/rcd330.c)), i.MX6 IPUv3 display controller ([qemu/hw/display/imx6_ipu.c](qemu/hw/display/imx6_ipu.c)), Atmel maXTouch T100 I2C touchscreen ([qemu/hw/input/atmel_mxt.c](qemu/hw/input/atmel_mxt.c)), and Renesas RH850 VIP MCU ([qemu/hw/misc/rcd330_vip.c](qemu/hw/misc/rcd330_vip.c)) |
| `src/hmi/` | Recreated `opt/hmi` (`5624_vw`) as C++; one `src/<binary>/` directory per recreated stock binary. [src/hmi/README.md](src/hmi/README.md), VA traceability index [src/hmi/TRACE.json](src/hmi/TRACE.json) |
| `tools/` | [build_qemu.sh](tools/build_qemu.sh), [build_gui.sh](tools/build_gui.sh), [build_shims.sh](tools/build_shims.sh), [build_rootfs.sh](tools/build_rootfs.sh), [build_hmi.sh](tools/build_hmi.sh), [run_rcd330.sh](tools/run_rcd330.sh), [export_public.sh](tools/export_public.sh), unified native Qt6 faceplate/display/CAN/coding app (`tools/rcd330_gui/`), VW Long Coding helper [rcd330_coding.py](tools/rcd330_coding.py), prebuilt guest binaries (`tools/prebuilt/`), and ARMv7 guest shims ([tools/arm_gl/gl_fb_shim.c](tools/arm_gl/gl_fb_shim.c), [tools/arm_hal_shim/hal_gpio_shim.c](tools/arm_hal_shim/hal_gpio_shim.c), [tools/arm_init/init.c](tools/arm_init/init.c)) |
| `resources/scenes/` | Extracted declarative IGDT scene-graph JSONs ([scenes_5624_vw.json](resources/scenes/scenes_5624_vw.json), [scenes_5606_skoda.json](resources/scenes/scenes_5606_skoda.json)) |
| `resources/translations/` | Extracted multi-language dictionaries (`en_US`, `de_DE`, `zh_CN`, `pt_BR`, `ru_RU`, `es_MX`) + full string table dumps (`strings_5624_vw.json`, `strings_5606_skoda.json`) |
| `docs/decompiled/` | Complete Ghidra 12.2 decompilation archives indexed in [docs/decompiled/README.md](docs/decompiled/README.md): **`5624/`** (`32` core binaries / `27,422` `.c` files: `vipImage.bin` [vip/README.md](docs/decompiled/5624/vip/README.md), `opt/hmi` `9,580` functions + [hmi_symbols_5624.json](docs/decompiled/5624/hmi/hmi_symbols_5624.json) + `gresfiles_catalog_{5624,5606}.json`, `opt/vgw`, `libHAL`, `libOSAL`, `hal_init`, `opt/vmf`, `libvmf_*`, `vmf_{send,rcv}`, and 21 `services/*`), **`diff-5624vs5606/`** (`20` differing core `5606_skoda` binaries / `23,776` `.c` files, [diff-5624vs5606/README.md](docs/decompiled/diff-5624vs5606/README.md)), and **`diff-5624vs780/`** (`12` differing core RCD 780 / `8.5.9.7` CR222 binaries / `21,042` `.c` files, [diff-5624vs780/README.md](docs/decompiled/diff-5624vs780/README.md)) |
| `docs/*.md` | Deep technical specs ([architecture.md](docs/architecture.md), [hmi-internals.md](docs/hmi-internals.md), [ipc-and-vgw.md](docs/ipc-and-vgw.md), [coding.md](docs/coding.md), [assets-and-localization.md](docs/assets-and-localization.md), [firmware-packaging-and-update.md](docs/firmware-packaging-and-update.md), [online-sources-summary.md](docs/online-sources-summary.md)) |
| `original-fw/`, `extracted-fw/`, `build/` | Gitignored local firmware tarballs, extracted rootfs (`5624_vw`, `5606_skoda`), and generated kernel/initramfs/ext2 build artifacts |

---

## 2. Verified Target Binary & Emulation Facts (`5624_vw`)

- **`opt/hmi` `VA -> File Offset`**: `PT_LOAD[0]` (`0x8000..0x95efa0`, `.text` & `.rodata`): `file_offset = vaddr - 0x8000`. `PT_LOAD[1]` (`0x967000..`, `.init_array`, `.dynamic`, `.got`, `.data`): `file_offset = vaddr - 0x10000`.
- **True Process Supervision Chain**: `/etc/rc.d/rcS` launches `/opt/camera &` early in boot, recovers/mounts `/pss1` and `/pss2`, remounts `/` read-only, and runs `/opt/launch.sh`. `launch.sh` starts `vmf`, `hal_init`, `pwr_agent -o -v`, `sysupdate`, and `mfid`. **`opt/pwr_agent` (`FUN_0000d59c` / `start_next_group_transition`) is the process supervisor** that spawns `vgw`, `pss`, `media`, `audio_proc`, `bt`, `hmi`, `diag_eol`, `diag`, `start_carplay.sh`, `connect_mgr`, `start_carlife.sh`, `exlap`, `launch_aa.sh`, and `start_mdnsd.sh` in staged power groups.
- **UI Rendering**: Original `opt/hmi` links `libQtCore.so.4` (no `libQtGui`) and uses **Qt 4.7.4 only for the event loop, signals/slots, threads, and timers**, drawing all graphics directly via **IGDT** (`0x31b000..0x32a500`) over Vivante EGL/GLES2 (`800x480`) + FreeType + CxImage (`libximage.so.1`). In QEMU, [tools/arm_gl/gl_fb_shim.c](tools/arm_gl/gl_fb_shim.c) implements the 11 `egl*`, 4 `fb*`, and 58 `gl*` calls and scans out to `/dev/fb0`.
- **Input Paths in `opt/hmi`**:
  - **Touchscreen**: `VMultiTouch::Init` (`0x238d4`) uses `tslib` on `TSLIB_TSDEVICE` (`/dev/event1` / `/dev/input/event1`, backed by `atmel_mxt_ts` on I2C4 `0x4b`), calibrated by `/etc/pointercal`.
  - **Physical Buttons**: `VKeyDriver::readKeyDriver` (`0xaa77c`) reads 16-byte `struct input_event` (`EV_KEY = 1`, codes `1=RADIO`, `2=MEDIA`, `3=VOICE/MUTE`, `4=SETUP`, `5=PHONE`, `6=TUNE_OK`, `7=TUNE_UP`, `8=TUNE_DOWN`, `9=EJECT`, `10=APP`) from `/dev/event0`.
  - **Tune Knob**: `VKnobDriver::readKnobDriver` (`0xaa3e0`) reads 4-byte ints (`0x32` = up, `0x33` = down) from `/dev/tuner_knob` (or `EV_KEY` codes `7`/`8` via `/dev/event0`).
- **Asset Table**: `VA 0x69b69c..0x7578ec` (`5624`, `32,110` slots / `2,806` unique `_ovg.bin`), `VA 0x6ca2f4` (`5606`, `39,367` slots / `2,611` unique). Deduplicate by picking `max(w * h)` per filename.
- **Compressed Strings**: `FUN_00023650` (`0x23650`) reads `file 0x3c3680` (`VA 0x3cb680`, max valid start id `0x20fcb`, block length `0x20fec`) in `5624` and `file 0x37bba0` (`VA 0x383ba0`, max start id `0x20c2c`) in `5606`. UTF-32 engineering strings live at `file 0x89cc00` (`VA 0x8a4c00`).
- **VMF Framing & VGW**: 26-byte header (`[0]=0x02`, `[0x14]=groupId`, `[0x15]=subId`, `*(uint16_t*)(+0x18)=len`, `payload` at `+0x1a`). All 18 `VBaseProxy` subsystem proxies in `opt/hmi` subscribe to RX groups via `FUN_0005fdd4` (`0x5fdd4`) and transmit via `FUN_0005ff4c` (`0x5ff4c`). See [docs/ipc-and-vgw.md](docs/ipc-and-vgw.md) for the complete group routing table.

---

## 3. Build & Emulation Workflows

```sh
# Build custom qemu-system-arm (-M rcd330):
./tools/build_qemu.sh

# Build (and optionally test) the native Qt6 unified display & control panel (build/gui/rcd330_gui):
./tools/build_gui.sh --test

# Build (or stage prebuilt) ARMv7 guest shims (libEGL/libGLESv2, libhal_gpio_shim, /sbin/init):
./tools/build_shims.sh

# Stage a retail firmware .tar into build/cache/<fw_id>/{zImage,rootfs.cpio.gz}:
./tools/build_rootfs.sh [path/to/firmware.tar]

# Launch QEMU (-M rcd330) + unified native Qt6 RCD 330 faceplate/display/CAN/coding window:
./tools/run_rcd330.sh [path/to/firmware.tar]

# Cross-build the recreated opt/hmi (build/hmi/hmi) + ABI gate; --host-tests runs the host unit tests instead:
./tools/build_hmi.sh [--force] [--host-tests [--no-assets]]

# Boot a recreated opt/hmi (src/hmi, built by tools/build_hmi.sh) headless; the variant
# rootfs goes to build/cache/<fw_id>/rootfs_hmi.cpio.gz, the stock cpio is untouched:
./tools/run_rcd330.sh --headless --hmi build/hmi/hmi [--monitor <unix-socket>] [--profile-env KEY=VAL]

# Fast Python ABI/table/trace gates (and slow QEMU boot + pixel-parity tests with RCD330_QEMU_TESTS=1):
python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v
RCD330_QEMU_TESTS=1 python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v
```

### Keeping Documentation & Repository Clean
- **Recreated binaries (`src/<binary>/`)**: Follow stock source paths and class names; put an origin tag `// 0x<8-hex VA> <Name>` on the line above every recreated function (plus `DEVIATION:`, `STOCK-QUIRK:`, or `TODO(phaseN):` comments where applicable). Regenerate `src/hmi/TRACE.json` with `python3 src/hmi/tools/gen_trace.py` after adding or moving origin tags (checked by `tools/tests/test_hmi_build.py`). Rule #1 (do not re-decompile) still holds.
- When investigating firmware behavior or fixing emulation bugs in `qemu/` or `tools/arm_*`, verify against the decompiled functions in `docs/decompiled/` and keep `docs/*.md` in sync with new findings.
- Never commit `original-fw/`, `extracted-fw/`, or `build/`.

### Private vs. Public Repository Sync
- **Always develop and commit on `main`** (never commit directly on `github-main`). `main` is the complete private branch (including all `docs/decompiled/**/functions/*.c`) synced to `origin`.
- **Never push `main` directly to `github`.** Use [tools/export_public.sh](tools/export_public.sh) to advance the stripped `github-main` branch from `main` without touching the working tree (it removes `docs/decompiled/**/functions/*`, `decompile-index.json`, and `decompile-journal.jsonl`, keeping all `README.md` guides and symbol metadata), then push both remotes:
  ```sh
  git push origin main
  ./tools/export_public.sh "Summary of public changes"
  git push github github-main:main
  ```
- **Public-clone hygiene**: Keep all committed docs and links repo-relative (no `file://` or machine-specific paths, no private remote URLs), ensure `python3 src/hmi/tools/gen_trace.py --check` continues to pass without `docs/decompiled/5624/hmi/functions/*.c` (via `docs/decompiled/5624/hmi/hmi_symbols_5624.json`), and if the `qemu/` submodule (`rcd330` branch) was updated, push `qemu/` to its public remote before syncing `github-main`.

---

## 4. Diffing a New Firmware Build or Dump Against `5624_vw`

When a new firmware `.tar` or full NAND/rootfs dump is added to `original-fw/`, follow this repeatable procedure (modeled on [docs/diff-5624-vs-rcd780.md](docs/diff-5624-vs-rcd780.md)):

1. **Extract locally (gitignored)**: Unpack into `extracted-fw/<id>/` (never commit `original-fw/` or `extracted-fw/`). Analyze only the firmware/dump artifacts (`mtd/`, `rootfs/`, or `.tar` payload), ignoring any bundled third-party docs or scripts.
2. **Identify build provenance & detect overlays**:
   - Inspect `/sysupdate/update_info.ini`, `/opt/stdplus.ini` (compare `[Version]` vs. runtime `[VIP]`), `/opt/feature_config.ini`, `opt/md_adapter` DWARF build paths, `mtd0.bin` U-Boot banner/HAB certs, and `mtd1.bin` / `kernelImage` Linux version + HAB trailer.
   - Cluster file mtimes across `opt/` and `opt/gresfiles/` to separate the base Visteon build from aftermarket/community overlays (watch for uninitialized-RTC epochs like `2016-04-24`, spoofed `stdplus.ini` versions, font `name`-table timestamps in `FZHT_GB18030.ttf`, and `.rodata` Qt slot/language hex-patches in `opt/hmi`).
   - Diff `opt/launch.sh`, `opt/config/*.conf`, `.ko` `vermagic` strings, `etc/{passwd,group,shadow,pointercal}`, and runtime `/pss1/*.pss` blocks (`DIAG_PSS_CODING.pss`, `FEATURE_MANAGER_PSS_BLK.pss`, `V_EOL_PSS_*`).
3. **Tree & ELF diff against `extracted-fw/5624_vw/`**:
   - Hash-diff shared subtrees (`opt/`, `etc/`, `usr/`, `lib/`, `mirrorlink/`, `var/`) and `opt/gresfiles/`, and collect every differing user-space ELF binary with file size and `.text`/`.rodata`/`.ARM.exidx` deltas.
   - Consult existing `docs/decompiled/5624/` exports for the `5624_vw` baseline (never re-decompile `5624_vw` binaries already archived there — Rule #1).
4. **Decompile differing core binaries into `docs/decompiled/diff-5624vs<id>/`**:
   - **Core-binaries-only archive scope**: Only archive decompiled outputs for binaries that belong to the **`32` core binaries tracked in `docs/decompiled/5624/`** (`hmi/`, `vgw/`, `vip/`, `hal_osal/*`, `vmf/*`, and the `21` core `services/*`), mirroring `docs/decompiled/5624/`'s exact directory layout so every `diff-*` archive stays a strict subset of `5624/`. Still document behavioral/version changes of non-core third-party SDK/helper binaries (`aap_service`, `libAirPlay*.so`, `libCarPlay*.so`, `libiap2.so`, `md_adapter`, `mdnsd`, `librace_iface.so`, etc.) in `docs/diff-5624-vs-<id>.md` without storing raw SDK `.c` dumps.
   - **Deduplicate across diff archives**: Before decompiling a differing core binary, check its SHA-256 against existing `docs/decompiled/diff-*` archives as well. If it is byte-identical to a binary already archived in another diff, do **not** store duplicate `functions/*.c` files — keep only a `README.md` linking to (or symlinking) the existing diff archive.
   - Use REA Ghidra 12.2 (`open_binary` → `decompile_all` with `layout: "address"` → `close_binary`) directly into `docs/decompiled/diff-5624vs<id>/<subdir>/` (and `tools/ghidra/vip_headless.sh` + `VipSetup.java` + `VipExport.java` for `vipImage.bin`).
   - **Large ELF batching** (`opt/hmi`, etc.): Start with a modest `limit` (e.g., `100`–`500`) so auto-analysis + first batch completes under the 3-minute MCP deadline and keeps the Ghidra session warm, then continue with `resume: true, limit: 2500..3500` until `remaining == 0`. Run `chmod -R a+rX docs/decompiled/diff-5624vs<id>` afterwards.
   - Compare decompiled pseudocode against `docs/decompiled/5624/` (matching by symbol/string/normalized body) to document concrete behavioral changes rather than raw size deltas.
5. **Document, index & verify**:
   - Write per-binary `README.md` summaries under `docs/decompiled/diff-5624vs<id>/`, an archive inventory `docs/decompiled/diff-5624vs<id>/README.md` (exact counts computed from `decompile-index.json` and disk `.c` files), and a full diff report `docs/diff-5624-vs-<id>.md`.
   - Link the new archive and report in [docs/decompiled/README.md](docs/decompiled/README.md), [README.md](README.md), and `AGENTS.md`.
   - Verify all repo-relative markdown links resolve (no `file://` or machine-specific paths), confirm [tools/export_public.sh](tools/export_public.sh)'s strip pattern (`^docs/decompiled/.*/(functions/|decompile-(index\.json|journal\.jsonl)$)`) covers the new subtree with `0` leaks, and run `python3 src/hmi/tools/gen_trace.py --check`.
