# Prebuilt ARMv7 Guest Binaries (`tools/prebuilt/`)

Fallback guest binaries staged into the QEMU initramfs by `tools/build_rootfs.sh` when an ARM cross-compiler (`arm-linux-gnueabi-gcc` or `zig`) is not installed on the host.

| File | Built From / Source | License | Notes |
|---|---|---|---|
| `libEGL.so.1` & `libGLESv2.so.2` | `tools/arm_gl/gl_fb_shim.c` | MIT | Rebuilt via `./tools/build_shims.sh --update-prebuilt` |
| `libhal_gpio_shim.so` | `tools/arm_hal_shim/hal_gpio_shim.c` | MIT | Rebuilt via `./tools/build_shims.sh --update-prebuilt` |
| `init` | `tools/arm_init/init.c` | MIT | Static musl `/sbin/init`, rebuilt via `./tools/build_shims.sh --update-prebuilt` |
| `busybox-armv5l` | Upstream [BusyBox](https://busybox.net/) static ARM binary | GPL-2.0 | Provides shell/mount/tar utilities inside the QEMU initramfs |
| `libQtCore.so.4` | Upstream [Qt 4.8.2](https://download.qt.io/archive/qt/4.8/) (`libqtcore4` armel) | LGPL-2.1 | Stand-in `libQtCore.so.4` matching the headunit's softfp ARMv7 ABI when staging firmware packages that omit `/usr/local/qte4.7.4/lib` |
