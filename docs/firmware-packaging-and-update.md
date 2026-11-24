# Firmware Packaging, `opt/sysupdate` & QEMU Rootfs Staging

Detailed specification of Noname/Visteon `.tar` update packages based on our complete `decompile_all` export of [`opt/sysupdate`](decompiled/services/sysupdate) (`2,267` functions, `0` failed), inspection of both original tarballs in `original-fw/`, and the QEMU emulation staging pipeline in `tools/`.

---

## 1. Archive Structure (Direct Flat POSIX/GNU Tar)

Unlike older MIB2 units using signed nested containers (`Signature.Hat` + inner archives), both `std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar` (`3,749` entries) and `std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar` (`3,487` entries) are **flat, unencrypted, unsigned tarballs**:

```text
<update>.tar
├── config.txt                  # Flags + list of filesystem subtrees to extract
├── script/
│   ├── before_app_update.sh    # Pre-extraction shell hook
│   └── after_app_update.sh     # Post-extraction shell hook (clears /pss2/app_data & /race)
├── opt/
│   ├── stdplus.ini             # [Version] AP_VER/CUSTM_PQ & [Upgrade] PACK_TYPE
│   ├── hmi
│   └── gresfiles/...
├── etc/, usr/, mirrorlink/, lib/firmware/   # Optional rootfs directories
├── kernelImage                 # Optional Linux uImage (flashed to mtd1 if kernel_update:1)
├── u-boot.bin                  # Optional U-Boot (flashed via kobs-ng if bootloader_update:1)
├── vipImage.bin                # Optional Renesas RH850 VIP firmware (1 MB)
└── bt_ac_firm/...              # Optional ALPS Bluetooth firmware
```

### Example `config.txt` (from `5624_vw`)
```ini
kernel_update:1
bootloader_update:1
vip_update:1
bt_update:1
middleware_update:1
[file]:[opt]
[file]:[etc]
[file]:[usr]
[file]:[mirrorlink]
[file]:[lib/firmware]
```
For software-only or HMI/translation overlay updates, `kernel_update:0`, `bootloader_update:0`, `vip_update:0`, `bt_update:0`, `middleware_update:1`, and `[file]:[opt]` are used.

---

## 2. How `opt/sysupdate` Validates & Flashes Packages

From [`FUN_000147fc`](decompiled/services/sysupdate/functions/0x147fc.c) (`IsPackageValid`), [`FUN_00010fac`](decompiled/services/sysupdate/functions/0x10fac.c) (`ParseConfigFile`), and [`FUN_000190fc`](decompiled/services/sysupdate/functions/0x190fc.c) (`ExtractFileToDir`):
1. Runs `tar -xvf "<pkg>.tar" <member> -C /sysupdate/system/` without checking any cryptographic signature in standard upgrade mode.
2. Requires **`opt/stdplus.ini`**, **`config.txt`**, and **`script/`** to be present in the archive.
3. Checks `[Upgrade] PACK_TYPE` (`VW_PQ_PSS`) matches the device and checks `[Version] AP_VER` differs from the currently flashed `AP_VER` ([`FUN_00011f70`](decompiled/services/sysupdate/functions/0x11f70.c)).
4. Writes `FLAG_UPDATE_HOST` (or `FLAG_UPDATE_VIP`) into `/sysupdate/flag.ini` and triggers a hard reboot through VIP watchdog shutdown via VMF `(0x8F, 0x19, payload=[0x01])`.
5. On reboot, `/etc/rc.d/rcS` inspects `/sysupdate/flag.ini`:
   - When `FLAG_UPDATE_HOST`, `FLAG_UPDATE_VIP`, or `FLAG_UPDATE_HOST_ERROR` is set, `rcS` remounts `/` read-write, restores `/sysupdate/backup/*` on error, starts only `./vmf &`, `./hal_init`, `./vgw &`, and `./sysupdate &`, executes `script/before_app_update.sh`, flashes enabled images (`u-boot.bin` via `kobs-ng`, `kernelImage` to `mtd1`, `vipImage.bin` over SPI, `bt_ac_firm`), untars `[file]:[...]` trees, and executes `script/after_app_update.sh`.

---

## 3. Staging Extracted Firmware for QEMU (`-M rcd330`)

To run the unmodified retail `5624_vw` ARMv7 binaries inside our custom QEMU machine ([qemu/hw/arm/rcd330.c](../qemu/hw/arm/rcd330.c)):

1. **Kernel Extraction & Direct `.tar` Staging ([tools/build_rootfs.sh](../tools/build_rootfs.sh))**:
   - Extracts any unmodified Noname/Visteon firmware `.tar` (default `original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar`) directly into `build/cache/<fw_id>/stage/`.
   - Strips the 64-byte U-Boot `uImage` header (`0x27051956`) from `kernelImage` when present (or copies raw `zImage` verbatim) to produce `build/cache/<fw_id>/zImage` (Linux 3.0.35/3.1.15 for Freescale i.MX6 Eagle, `MACH_TYPE 4400`).
2. **ARM Guest Shims ([tools/build_shims.sh](../tools/build_shims.sh))**:
   - **[tools/arm_gl/gl_fb_shim.c](../tools/arm_gl/gl_fb_shim.c)** (`libEGL.so.1` & `libGLESv2.so.2`, Cortex-A9 NEON softfp): Implements the 11 `egl*`, 4 Vivante `fb*`, and 58 `gl*` symbols used by IGDT in `opt/hmi`, rasterizing textured/tinted quads, FBOs, scissor boxes, and fonts in software with dirty-row partial `/dev/fb0` scanout on `eglSwapBuffers`.
   - **[tools/arm_hal_shim/hal_gpio_shim.c](../tools/arm_hal_shim/hal_gpio_shim.c)** (`libhal_gpio_shim.so`, loaded via `LD_PRELOAD`):
     - Redirects `/sys/class/gpio/*` to `/tmp/gpio_emu/*` so `libHAL.so` GPIO calls succeed without `EBUSY`.
     - Bridges `SPI_IOC_MESSAGE` on `/dev/spidev0.0` (and `/dev/spidev1.0`) to `rcd330-vip` MMIO (`0x021fc000`), uses real `/dev/v850srq` (`GPIO2_8` IRQ `0x128`) when available, and wakes `SIGIO` cleanly for outbound `tx_msg_id` frames, VIP RX queue, and single-shot ACK completion.
     - Provides a virtual `/dev/event0` pipe fed by `rcd330-vip` MMIO (`0x021fc020`) for physical faceplate buttons (while `TUNE` rotary steps use the hardware `GPIO3_24`/`GPIO3_25` quadrature lines to `/dev/tuner_knob`), and injects volume/mute changes (`0x021fc024`) into VMF `(0x3F, 0x00)`.
   - **[tools/arm_init/init.c](../tools/arm_init/init.c)** (`/sbin/init`): Minimal static ARM init that mounts `/proc`, `/sys`, `/dev`, `/tmp`, creates essential device nodes, sets `LD_PRELOAD=/opt/lib/libhal_gpio_shim.so`, and runs the unmodified retail `/etc/rc.d/rcS` -> `/opt/launch.sh` -> `opt/pwr_agent` chain.
3. **Factory Base Layer & Initramfs Packaging ([tools/build_rootfs.sh](../tools/build_rootfs.sh))**:
   - Caches the Factory Base Layer (`build/factory_base.cpio.gz` / `build/factory_base_stage/`) with static `busybox-armv5l` applets, UBI no-op wrappers, safe `/bin/mount` wrapper, guest shims, and `libQtCore.so.4`, overlaid symlink-safely (`tar -Uxf -`) onto the firmware `.tar` payload.
   - Preserves factory `/etc/pointercal`, `/lib/firmware/maxtouch.cfg`, `/etc/rc.d/rcS`, and `/opt/launch.sh` unmodified, seeds `/pss1` from `/opt/{stdplus.ini,config,hmi_autotest.ini}`, strips installer-only files (`u-boot.bin`, `vipImage.bin`, `config.txt`, `script/`), and generates `build/cache/<fw_id>/rootfs.cpio.gz`.
4. **Launching the Emulator & Native Control Panel ([tools/run_rcd330.sh](../tools/run_rcd330.sh) & [tools/rcd330_gui/](../tools/rcd330_gui/CMakeLists.txt))**:
   ```sh
   ./tools/run_rcd330.sh [path/to/firmware.tar]
   ```
   Builds `build/gui/rcd330_gui` if needed, boots `qemu/build/qemu-system-arm -M rcd330` headlessly with localhost VNC (`127.0.0.1:5900`) and the `rcd330-vip` chardev socket (`127.0.0.1:4444`), and launches the unified native Qt6 RCD 330 faceplate/display/CAN/coding window in the foreground (shutting QEMU down cleanly on window close).
