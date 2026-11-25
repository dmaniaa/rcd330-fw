[← Decompiled Firmware Index](../../README.md)

# `opt/lib/libHAL.so.1.0.0` — Hardware Abstraction Library (`5624_vw`)

- **Target binary**: `opt/lib/libHAL.so.1.0.0` (`34,836` bytes, ARMv7-A shared library, SHA-256 `b6b57963f5e7ddf7eb1e02781016e4ec0e8beee921fb14e5ebad7a98727b8899`)
- **Decompiled**: `187` written / `237` total / `50` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Deep specs**: [architecture.md](../../../../architecture.md) · [ipc-and-vgw.md](../../../../ipc-and-vgw.md) · [firmware-packaging-and-update.md](../../../../firmware-packaging-and-update.md)

## Overview

Shared hardware abstraction library initialized at boot by `opt/hal_init` and linked by `opt/vgw` and hardware-facing services. Provides uniform Linux userspace wrappers for:
- **Sysfs GPIO & PWM/VBUS** (`/sys/class/gpio/export`, `unexport`, `gpio%d/direction`, `gpio%d/value`)
- **SPI IPCL** to the Renesas RH850 VIP MCU over `/dev/spidev0.0`, `/dev/v850srq` (`SIGIO` interrupt line, `GPIO2_8`), and GPIO 83 chip-select/handshake, using framed CRC-16/CCITT-FALSE packets (`[total_be, sub_len_be, grp, sub, payload..., crc16_be]`)
- **I2C** (`/dev/i2c-%d`) and **UART** serial interfaces

In QEMU (`-M rcd330`), `tools/arm_hal_shim/hal_gpio_shim.c` (`LD_PRELOAD`) intercepts the `/sys/class/gpio/*` and `SPI_IOC_MESSAGE` calls issued by `libHAL.so.1.0.0` to connect `opt/vgw` to the emulated `rcd330-vip` device.

## Recovered Source Files

- `hal_code/src/hal_gpio.c`
- `hal_code/src/hal_spi.c`
- `hal_code/src/hal_uart.c`

## Key Exported Functions

- **GPIO / PWM / VBUS**: [`0x123fc`](functions/0x123fc.c) (`hal_gpio_init`), [`0x12678`](functions/0x12678.c) (`hal_gpio_open`), [`0x13090`](functions/0x13090.c) (`hal_gpio_read`), [`0x13168`](functions/0x13168.c) (`hal_gpio_write`), [`0x12a3c`](functions/0x12a3c.c) (`hal_gpio_close`), [`0x12c24`](functions/0x12c24.c)..[`0x12fe4`](functions/0x12fe4.c) (`hal_pwminput_*`), [`0x13310`](functions/0x13310.c)..[`0x135c4`](functions/0x135c4.c) (`hal_vbus_*`).
- **SPI IPCL (VIP Link)**: [`0x1439c`](functions/0x1439c.c) (`spi_ipcl_init`), [`0x13dd4`](functions/0x13dd4.c) (`spi_ipcl_communication`), [`0x14724`](functions/0x14724.c) (`spi_ipcl_write`), [`0x14880`](functions/0x14880.c) (`spi_ipcl_read`), [`0x14684`](functions/0x14684.c) (`spi_ipcl_close`).
- **I2C & UART**: [`0x14cc4`](functions/0x14cc4.c) (`hal_i2c_init`), [`0x14d28`](functions/0x14d28.c)..[`0x151e4`](functions/0x151e4.c) (`hal_i2c_open`/`read`/`write`/`close`), [`0x15790`](functions/0x15790.c) (`hal_uart_init`).
