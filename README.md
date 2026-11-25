# RCD 330 QEMU Firmware Emulator & Alternative Headunit Software Project

Full-system **QEMU hardware emulator (`-M rcd330`)**, complete **reverse-engineering knowledge base & decompilation archive (`docs/`)**, and upcoming **alternative headunit software** for the **Volkswagen / ŠKODA RCD 330 Noname / Visteon (`6RD 035 187 B/C/E/F`)** infotainment headunit.

Based on VW firmware build **`5624`** (`std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar`) and ŠKODA firmware build **`5606`** (`std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar`).

---

## Project Overview & Current State

This project consists of two main pieces of software supported by a complete reverse-engineering archive:

1. **QEMU-Based Full-System Emulator (`qemu/` + `tools/`) — *Active Development***:
   - Boots the **unmodified original retail Linux 3.0.35/3.0.38 kernel (`kernelImage`) and ARMv7 userland (`opt/vmf`, `opt/hal_init`, `opt/vgw`, `opt/pwr_agent`, `opt/sysupdate`, `opt/hmi`)** inside a custom QEMU machine (`-M rcd330`).
   - Emulates the Freescale i.MX6 Eagle board (`MACH_TYPE 4400`), i.MX6 IPUv3 display controller (`800x480` `/dev/fb0`), Atmel maXTouch T100 I2C capacitive touchscreen (`atmel.mxt` at `0x4b` on I2C4), `ADV7182` RVC video decoder stub, and the Renesas RH850 Vehicle Interface Processor (`rcd330-vip`).
   - Includes a lightweight ARMv7 guest OpenGL ES 2.0 / EGL software framebuffer renderer ([tools/arm_gl/gl_fb_shim.c](tools/arm_gl/gl_fb_shim.c)) for Visteon's proprietary **IGDT** graphics engine, an `LD_PRELOAD` HAL/SPI IPCL/GPIO/button shim ([tools/arm_hal_shim/hal_gpio_shim.c](tools/arm_hal_shim/hal_gpio_shim.c)), a unified native **Qt6** headunit window (`tools/rcd330_gui/`) embedding the live 800×480 touchscreen inside the virtual RCD 330 faceplate with press-and-hold & right-click quick-hold hardware buttons (`2.5 s` Long, `4.5 s` Service Mode, `10.0 s` Engineering Testmode, `10.5 s` `POWER` cold reboot), CAN/BAP simulator, and **Headunit Coding (`DID 0x0600`) / VRM / Adaptation editor**, backed by persistent coding storage (`build/coding/<fw_id>.json`, pre-seeded into `/pss1` & `/pss2` on rootfs staging via [tools/rcd330_coding.py](tools/rcd330_coding.py)).
   - **Current Status**: Boots the stock `5624_vw` firmware on `CL_15` ignition, renders the 800x480 display, supports touchscreen & faceplate buttons/knobs, top-bar volume overlay, exterior temperature, RVC & OPS parking overlays controlled by live/persistent VW Long Coding, ignition OFF/ON standby & wake, and System Information versions (Part Number, Hardware, Software, Bluetooth, CarPlay, Android Auto).
2. **Recreated Headunit Software (`src/hmi/`) — *Phase 0–1 Complete***:
   - Clean-room C++ recreation of stock `opt/hmi` (`5624_vw`, ARMv7 softfp, glibc 2.13 / GCC 4.6 libstdc++ ABI) cross-built with `zig` via `tools/build_hmi.sh`, including the IGDT GLES2 renderer, `_ovg.bin` texture loader, FreeType 2.4.8 text pipeline, and QEMU pixel-parity tests (~99.94% parity on `AppNoConn`). See [src/hmi/README.md](src/hmi/README.md).
3. **Complete Reverse Engineering Archive (`docs/` & `resources/`)**:
   - **All 31 ARMv7 ELF binaries and libraries** plus the **Renesas RH850 VIP MCU firmware (`vipImage.bin`)** from `5624_vw` (`27,422` total functions analyzed) are documented with per-binary `README.md` guides, symbol/asset metadata, and cross-cutting specs under [docs/decompiled/](docs/decompiled/README.md) *(raw Ghidra `functions/*.c` dumps are intentionally omitted from the public GitHub mirror; see [docs/decompiled/README.md](docs/decompiled/README.md))*.
   - **100% decoded `_ovg.bin` UI texture catalogs** ([gresfiles_catalog_5624.json](docs/decompiled/5624/hmi/gresfiles_catalog_5624.json), [gresfiles_catalog_5606.json](docs/decompiled/5624/hmi/gresfiles_catalog_5606.json)), declarative **IGDT scene graphs** ([resources/scenes/](resources/scenes)), and **multi-language string dictionaries** ([resources/translations/](resources/translations)).

---

## Quickstart: Running the Original Firmware in QEMU

### 1. Clone (with the modified QEMU submodule)
```sh
git clone --recurse-submodules --shallow-submodules https://github.com/dmaniaa/rcd330-fw.git
cd rcd330-fw
```

### 2. Prerequisites
- **macOS**:
  ```sh
  brew install ninja meson cmake qt pkg-config glib pixman python3 e2fsprogs
  ```
- **Debian / Ubuntu Linux**:
  ```sh
  sudo apt-get update && sudo apt-get install -y \
    git build-essential cmake ninja-build meson pkg-config qt6-base-dev e2fsprogs cpio \
    python3 python3-venv libglib2.0-dev libpixman-1-dev libslirp-dev libfdt-dev
  ```
- *(Optional, for rebuilding ARM guest shims from source instead of using `tools/prebuilt/`)*: `arm-linux-gnueabi-gcc` or `zig`.
- Place original firmware `.tar` packages in `original-fw/` (e.g. `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` or `original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar`, gitignored).

### 3. Build & Launch the Emulator
The one-command launcher automatically builds `qemu/build/qemu-system-arm` (`-M rcd330`) and the native Qt6 window (`build/gui/rcd330_gui`) if needed, extracts `zImage` and stages the cached initramfs directly from a retail firmware `.tar` under `build/cache/<fw_id>/`, and opens the unified RCD 330 faceplate + display + CAN/Coding window (closing the window shuts down QEMU cleanly):

```sh
./tools/run_rcd330.sh [path/to/firmware.tar]
```

Or run the individual build steps manually:
```sh
# 1. Configure and build qemu-system-arm with the rcd330 machine:
./tools/build_qemu.sh

# 2. Build and test the native Qt6 display & control panel:
./tools/build_gui.sh --test

# 3. Build (or copy prebuilt) ARM guest shims (libEGL/libGLESv2, libhal_gpio_shim, /sbin/init):
./tools/build_shims.sh

# 4. Stage a retail firmware .tar into build/cache/<fw_id>/{zImage,rootfs.cpio.gz}:
./tools/build_rootfs.sh [path/to/firmware.tar]

# 5. Launch QEMU + unified native RCD 330 window:
./tools/run_rcd330.sh [path/to/firmware.tar]
```

---

## Documentation Index

- [Architecture, Boot Chain & Process Map](docs/architecture.md)
- [`opt/hmi` Internals (Qt4 + IGDT GLES2, Input & All 18 VMF Proxies)](docs/hmi-internals.md)
- [VMF IPC Protocol & `opt/vgw` SPI IPCL Routing Matrix](docs/ipc-and-vgw.md)
- [Headunit Coding (`DID 0x0600`), Feature Manager & RVC/OPS Wire Spec](docs/coding.md)
- [IGDT Assets (`_ovg.bin`), Scene Graphs & Multi-Language Strings](docs/assets-and-localization.md)
- [Firmware Packaging (`opt/sysupdate`) & QEMU Rootfs Staging](docs/firmware-packaging-and-update.md)
- [Community & Online Sources Synthesis](docs/online-sources-summary.md)
- [Known Firmware Bugs: Android Auto BT Call Echo (ALPS ECNR State Machine)](docs/bugs/android-auto-echo-ecnr.md)
- [Decompiled Binary Index (`5624/` + `diff-5624vs5606/` + `diff-5624vs780/`)](docs/decompiled/README.md)
- [5624 (VW) vs. 5606 (ŠKODA `7.1.2.2` + `DE_CV`) Firmware Diff](docs/diff-5624-vs-5606.md)
- [5624 (VW) vs. RCD 780 (`8.5.9.7` CR222 + `5814` DE Overlay) Firmware Diff](docs/diff-5624-vs-rcd780.md)
- [AI Agent Guide (`AGENTS.md`)](AGENTS.md)
- [Recreated `opt/hmi` Guide (`src/hmi/README.md`)](src/hmi/README.md)

---

## License

Original code, scripts, shims, Qt6 control panel, recreated HMI source (`src/hmi/`), and documentation are licensed under the [MIT License](LICENSE). Third-party components retain their upstream licenses — including the modified QEMU submodule (`GPL-2.0`) and prebuilt guest helpers in [tools/prebuilt/](tools/prebuilt/README.md) (`BusyBox` GPL-2.0, `libQtCore.so.4` LGPL-2.1). No retail firmware images are included in this repository.
