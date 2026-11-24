# Hardware GPIO/IRQ Wiring (`v850srq`, `tuner_knob`, `matrix-keypad`) & Factory `atmel_mxt` Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the exact i.MX6 GPIO and IRQ lines expected by the Visteon Eagle Linux 3.0.35/3.1.15 kernel (`v850srq` on `GPIO2_8`/GPIO 40, `tuner_knob` quadrature encoder on `GPIO3_24` & `GPIO3_25`/GPIO 88 & 89, `matrix-keypad` on `GPIO3_20..21`, `GPIO2_21..22`, `GPIO5_2`), transform `atmel_mxt` T100 coordinates to match the untouched factory `/etc/pointercal` and `/lib/firmware/maxtouch.cfg`, and remove the polling threads and `/etc/pointercal` overrides from the guest.

**Architecture:** Reverse-engineering `extracted-fw/5624_vw/kernelImage` (`vmlinux`) proved that the kernel already has compiled-in drivers for `/dev/v850srq` (char major `200`, falling-edge IRQ `0x128` = `GPIO2_8` / GPIO 40), `/dev/tuner_knob` (char major `265`, rising+falling IRQ `0x158` = `GPIO3_24` / GPIO 88 `KNOB_A` + `GPIO3_25` / GPIO 89 `KNOB_B`), and `/dev/input/event0` (`matrix-keypad` on rows GPIO 84/85 and active-low cols GPIO 53/54/130). We expose these GPIO lines on `rcd330-vip` and wire them in `qemu/hw/arm/rcd330.c`, update `qemu/hw/input/atmel_mxt.c` so its T100 touch reports invert the factory `/etc/pointercal` transform (`775 -66330 53052216 -67001 -250 31844040 65536 800 480`), and stop overwriting `/etc/pointercal` or deleting `/lib/firmware/maxtouch.cfg` in `tools/build_rootfs.sh`.

**Tech Stack:** C (QEMU device models in `qemu/hw/arm/rcd330.c`, `qemu/hw/misc/rcd330_vip.c`, `qemu/hw/input/atmel_mxt.c`, `qemu/hw/gpio/imx_gpio.c`, and `tools/arm_hal_shim/hal_gpio_shim.c`), Bash (`tools/build_rootfs.sh`), Python 3 (`tools/tests/test_plan2_hardware_gpio_input.py`).

**Spec:** `docs/architecture.md`, `docs/hmi-internals.md`, `docs/decompiled/hmi/functions/0xaa77c.c`, `docs/decompiled/hmi/functions/0xaa71c.c`, `docs/decompiled/hmi/functions/0xaa3e0.c`, `docs/decompiled/hal_osal/libHAL/functions/0x1439c.c`

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- `v850srq` in the Eagle kernel (`0x801fe3c8`) requests IRQ `0x128` (`296` = `GPIO2_8` / GPIO 40) with `IRQF_TRIGGER_FALLING` (`2`). Therefore `rcd330-vip`'s `"srq"` line must idle HIGH (`1`) on reset, pulse LOW (`0`) when asserting SRQ, and return HIGH (`1`) on the timer callback.
- `tuner_knob` in the Eagle kernel (`0x8001dfdc` / ISR `0x80200400`) requests `KNOB_A` = GPIO 88 (`GPIO3_24`, IRQ `0x158` / `344`, `IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING`) and `KNOB_B` = GPIO 89 (`GPIO3_25`). When `KNOB_A` transitions, the kernel ISR reads `val_a = gpio_get_value(88)` and `val_b = gpio_get_value(89)`:
  - If `val_a != val_b`, the kernel queues `0x32` (`TUNE_UP` / clockwise).
  - If `val_a == val_b`, the kernel queues `0x33` (`TUNE_DOWN` / counter-clockwise).
- Factory `/etc/pointercal` (`775 -66330 53052216 -67001 -250 31844040 65536 800 480`) and `/lib/firmware/maxtouch.cfg` in the staged rootfs must remain **100% unmodified** from the firmware archive.

## Review Focus

- In `qemu/hw/gpio/imx_gpio.c`, `imx_gpio_reset` sets `s->psr = 0`. If `v850srq` (`GPIO2_8`) idles HIGH (`1`), lowering it to `0` triggers a falling-edge interrupt in `imx_gpio_set_int_line` (`extract64(s->icr, 2*line + 1, 1) == 1` and `extract64(s->icr, 2*line, 1) == 1` for falling edge, where `level == 0 != icr_bit0`).
- In `qemu/hw/input/atmel_mxt.c`, when `/lib/firmware/maxtouch.cfg` is kept in the rootfs, the kernel driver `atmel_mxt_ts` writes configuration bytes to `T7`, `T8`, `T100`, etc., and then writes `BACKUPNV` (`T6+1`) and `RESET` (`T6+0`). `atmel_mxt_send` must preserve the T100 `tchaux = 0x04`, `xrange = 799`, `yrange = 479` (or ignore overwrites of `MXT_T100_ADDR` config fields that would zero `xrange`/`yrange`) so `atmel_mxt_ts` continues decoding T100 finger messages properly after `maxtouch.cfg` upload!
- For `tuner_knob` (`ENC TUNE <delta>`), `rcd330-vip` should drive both the `knob-a`/`knob-b` GPIO outputs (for the kernel's `/dev/tuner_knob` driver read by `VKnobDriver` @ `0xaa3e0`) AND keep `key_q` / `matrix-keypad` fallback compatibility so both paths work reliably.

---

### Task 1: Wire `v850srq` (`GPIO2_8`) and `tuner_knob` (`GPIO3_24`, `GPIO3_25`) Between `rcd330-vip` and `FslIMX6State`

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c:40-48,129-140,388-401,603-635`
- Modify: `qemu/hw/arm/rcd330.c:297-310`
- Test: `tools/tests/test_plan2_hardware_gpio_input.py`

**Interfaces:**
- Consumes: `qdev_init_gpio_out_named` in `qemu/hw/misc/rcd330_vip.c` and `qdev_get_gpio_in(DEVICE(&s->soc.gpio[bank]), pin)` in `qemu/hw/arm/rcd330.c`.
- Produces:
  - Active-low `"srq"` GPIO output on `rcd330-vip` connected to `s->soc.gpio[1]` pin `8` (`GPIO2_8`, Linux GPIO 40 / IRQ `0x128`).
  - `"knob-a"` and `"knob-b"` GPIO outputs on `rcd330-vip` connected to `s->soc.gpio[2]` pins `24` and `25` (`GPIO3_24` / GPIO 88 and `GPIO3_25` / GPIO 89).

- [ ] **Step 1: Write the failing test in `tools/tests/test_plan2_hardware_gpio_input.py`**

```python
#!/usr/bin/env python3
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan2HardwareGpioInput(unittest.TestCase):
    def test_rcd330_wires_srq_and_tuner_knob_gpios(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        # GPIO2_8 (bank index 1, pin 8) for v850srq
        self.assertIn('"srq"', rcd330_c)
        self.assertIn("s->soc.gpio[1]), 8", rcd330_c)
        # GPIO3_24 and GPIO3_25 (bank index 2, pins 24 & 25) for tuner_knob
        self.assertIn('"knob-a"', rcd330_c)
        self.assertIn('"knob-b"', rcd330_c)
        self.assertIn("s->soc.gpio[2]), 24", rcd330_c)
        self.assertIn("s->soc.gpio[2]), 25", rcd330_c)
        self.assertIn("knob_a_irq", vip_c)
        self.assertIn("knob_b_irq", vip_c)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan2_hardware_gpio_input.py -v`
Expected: FAIL (`"srq"` not wired in `rcd330.c`).

- [ ] **Step 3: Implement active-low `srq` and quadrature `knob-a`/`knob-b` on `rcd330-vip` and connect them in `qemu/hw/arm/rcd330.c`**

In `qemu/hw/misc/rcd330_vip.c`:
1. Add `qemu_irq knob_a_irq;`, `qemu_irq knob_b_irq;`, and `bool knob_a_level;` to `struct Rcd330VipState`.
2. In `vip_pulse_srq(Rcd330VipState *s)`, pulse active-low (since the kernel `v850srq` driver uses `IRQF_TRIGGER_FALLING`):
```c
static void vip_pulse_srq(Rcd330VipState *s)
{
    qemu_irq_lower(s->srq_irq);
    timer_mod(s->srq_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}

static void vip_srq_timer_cb(void *opaque)
{
    Rcd330VipState *s = RCD330_VIP(opaque);
    qemu_irq_raise(s->srq_irq);
}
```
3. Add a helper `vip_step_tuner_knob(Rcd330VipState *s, bool up)` that sets `KNOB_B` (`GPIO 89`) relative to the new `KNOB_A` (`GPIO 88`) level before toggling `KNOB_A` (so the kernel ISR at `0x80200400` sees `KNOB_A != KNOB_B` -> `0x32` for `up == true`, and `KNOB_A == KNOB_B` -> `0x33` for `up == false`):
```c
static void vip_step_tuner_knob(Rcd330VipState *s, bool up)
{
    bool next_a = !s->knob_a_level;
    bool next_b = up ? !next_a : next_a;
    qemu_set_irq(s->knob_b_irq, next_b ? 1 : 0);
    s->knob_a_level = next_a;
    qemu_set_irq(s->knob_a_irq, next_a ? 1 : 0);
}
```
Call `vip_step_tuner_knob(s, a >= 0)` inside the `ENC TUNE` loop in `vip_process_cmd`.
4. In `vip_realize`, initialize `"knob-a"` and `"knob-b"`:
```c
    qdev_init_gpio_out_named(DEVICE(dev), &s->srq_irq, "srq", 1);
    qdev_init_gpio_out_named(DEVICE(dev), &s->knob_a_irq, "knob-a", 1);
    qdev_init_gpio_out_named(DEVICE(dev), &s->knob_b_irq, "knob-b", 1);
```
And in `vip_reset`, idle `s->srq_irq` high (`qemu_irq_raise(s->srq_irq);`) and set `s->knob_a_level = false; qemu_set_irq(s->knob_a_irq, 0); qemu_set_irq(s->knob_b_irq, 0);`.

In `qemu/hw/arm/rcd330.c`:
Connect `vip`'s `"srq"`, `"knob-a"`, and `"knob-b"` outputs:
```c
                DeviceState *vip = qdev_new("rcd330-vip");
                qdev_realize_and_unref(vip, BUS(spi_bus), &error_fatal);
                qdev_connect_gpio_out_named(
                    vip, "srq", 0,
                    qdev_get_gpio_in(DEVICE(&s->soc.gpio[1]), 8));
                qdev_connect_gpio_out_named(
                    vip, "knob-a", 0,
                    qdev_get_gpio_in(DEVICE(&s->soc.gpio[2]), 24));
                qdev_connect_gpio_out_named(
                    vip, "knob-b", 0,
                    qdev_get_gpio_in(DEVICE(&s->soc.gpio[2]), 25));
                memory_region_add_subregion(get_system_memory(), 0x021fc000,
                                            rcd330_vip_mmio(vip));
```

- [ ] **Step 4: Build QEMU and run the test**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_plan2_hardware_gpio_input.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/arm/rcd330.c hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/arm/rcd330: wire v850srq (GPIO2_8) and tuner_knob (GPIO3_24/25) to rcd330-vip"
```

---

### Task 2: Support Unmodified Factory `/etc/pointercal` and `/lib/firmware/maxtouch.cfg` in `qemu/hw/input/atmel_mxt.c`

**Files:**
- Modify: `qemu/hw/input/atmel_mxt.c:124-227`
- Modify: `tools/build_rootfs.sh:85-115`
- Modify: `tools/tests/test_plan2_hardware_gpio_input.py`

**Interfaces:**
- Consumes: Factory `/etc/pointercal` (`775 -66330 53052216 -67001 -250 31844040 65536 800 480`) and `/lib/firmware/maxtouch.cfg`.
- Produces:
  - In `qemu/hw/input/atmel_mxt.c`: Inverse-pointercal coordinate mapping in `mxt_push_touch_msg` so a QEMU click at screen pixel `(sx, sy)` (`0..799`, `0..479`) produces the exact `(raw_x, raw_y)` that the kernel's `atmel_mxt_ts` (`mt_x = 479 - raw_y`, `mt_y = 799 - raw_x`) and `tslib` (using the untouched factory `/etc/pointercal`) map back to `(sx, sy)`.
  - In `atmel_mxt_send`: Re-apply `mxt_enforce_t100_range(s)` whenever the guest writes to `MXT_T100_ADDR..MXT_T100_ADDR+55` or triggers T6 `RESET`/`BACKUPNV`, so the kernel's `/lib/firmware/maxtouch.cfg` download completes cleanly without corrupting T100 report formatting.
  - In `tools/build_rootfs.sh`: Remove `rm -f "$STAGE_DIR/lib/firmware/maxtouch.cfg"` and remove overwriting `$STAGE_DIR/etc/pointercal`.

- [ ] **Step 1: Add factory `pointercal` and `maxtouch.cfg` preservation tests to `tools/tests/test_plan2_hardware_gpio_input.py`**

Append to `TestPlan2HardwareGpioInput` in `tools/tests/test_plan2_hardware_gpio_input.py`:

```python
    def test_atmel_mxt_handles_factory_pointercal_and_maxtouch_cfg(self):
        mxt_c = (ROOT / "qemu/hw/input/atmel_mxt.c").read_text()
        self.assertIn("mxt_screen_to_raw", mxt_c)
        self.assertIn("mxt_enforce_t100_range", mxt_c)

    def test_build_rootfs_preserves_factory_pointercal_and_maxtouch_cfg(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        self.assertNotIn('rm -f "$STAGE_DIR/lib/firmware/maxtouch.cfg"', build_sh)
        self.assertNotIn('> "$STAGE_DIR/etc/pointercal"', build_sh)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan2_hardware_gpio_input.py -v`
Expected: FAIL (`mxt_screen_to_raw` not in `atmel_mxt.c` and `build_rootfs.sh` still overwrites `pointercal`).

- [ ] **Step 3: Implement `mxt_screen_to_raw` and `mxt_enforce_t100_range` in `qemu/hw/input/atmel_mxt.c` and remove overrides from `tools/build_rootfs.sh`**

In `qemu/hw/input/atmel_mxt.c`:
1. Factor out `mxt_enforce_t100_range(AtmelMxtState *s)`:
```c
static void mxt_enforce_t100_range(AtmelMxtState *s)
{
    s->mem[MXT_T100_ADDR + 1] = 0x83; /* CTRL: ENABLE | RPTEN */
    s->mem[MXT_T100_ADDR + 3] = 0x04; /* TCHAUX: area at +6 */
    s->mem[MXT_T100_ADDR + 13] = (MXT_WIDTH - 1) & 0xff;
    s->mem[MXT_T100_ADDR + 14] = ((MXT_WIDTH - 1) >> 8) & 0xff;
    s->mem[MXT_T100_ADDR + 24] = (MXT_HEIGHT - 1) & 0xff;
    s->mem[MXT_T100_ADDR + 25] = ((MXT_HEIGHT - 1) >> 8) & 0xff;
}
```
Call `mxt_enforce_t100_range(s)` at the end of `mxt_init_info_block(s)` and inside `atmel_mxt_send()` after writing to `s->mem[s->reg_addr]`.
2. Add `mxt_screen_to_raw(int32_t sx, int32_t sy, uint16_t *raw_x_out, uint16_t *raw_y_out)` before `mxt_push_touch_msg`:
```c
/*
 * Inverts the factory /etc/pointercal linear transform:
 *   a = [775, -66330, 53052216, -67001, -250, 31844040, 65536]
 *   sx * 65536 =  775 * mt_x - 66330 * mt_y + 53052216
 *   sy * 65536 = -67001 * mt_x - 250 * mt_y + 31844040
 * combined with 5624 atmel_mxt_ts driver axis mapping:
 *   mt_x = 479 - raw_y,  mt_y = 799 - raw_x
 */
static void mxt_screen_to_raw(int32_t sx, int32_t sy,
                              uint16_t *raw_x_out, uint16_t *raw_y_out)
{
    const int64_t a0 = 775, a1 = -66330, a2 = 53052216;
    const int64_t a3 = -67001, a4 = -250, a5 = 31844040, a6 = 65536;
    const int64_t det = a0 * a4 - a1 * a3; /* -4444370080 */
    int64_t rhs_x = (int64_t)sx * a6 - a2;
    int64_t rhs_y = (int64_t)sy * a6 - a5;
    int32_t mt_x = (int32_t)((rhs_x * a4 - rhs_y * a1) / det);
    int32_t mt_y = (int32_t)((a0 * rhs_y - a3 * rhs_x) / det);
    int32_t rx = (MXT_WIDTH - 1) - mt_y;
    int32_t ry = (MXT_HEIGHT - 1) - mt_x;
    if (rx < 0) rx = 0;
    if (rx >= MXT_WIDTH) rx = MXT_WIDTH - 1;
    if (ry < 0) ry = 0;
    if (ry >= MXT_HEIGHT) ry = MXT_HEIGHT - 1;
    *raw_x_out = (uint16_t)rx;
    *raw_y_out = (uint16_t)ry;
}
```
Use `mxt_screen_to_raw(s->cur_x, s->cur_y, &raw_x, &raw_y)` in `mxt_push_touch_msg` when populating `t5[2..5]`.
3. In `tools/build_rootfs.sh`, remove `rm -f "$STAGE_DIR/lib/firmware/maxtouch.cfg"` and remove `echo "0 -65536 52363264 -65536 0 31391744 65536 800 480" > "$STAGE_DIR/etc/pointercal"`.

- [ ] **Step 4: Build QEMU and run the tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_plan2_hardware_gpio_input.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/input/atmel_mxt.c
git -C qemu commit -m "hw/input/atmel_mxt: map coordinates for factory pointercal and preserve T100 config"
git add tools/build_rootfs.sh tools/tests/test_plan2_hardware_gpio_input.py
git commit -m "tools/build_rootfs: preserve unmodified factory pointercal and maxtouch.cfg"
```

---

### Task 3: Replace Unconditional 50 ms `SIGIO` Polling in `hal_gpio_shim.c` with Event-Driven Delivery & Matrix-Keypad GPIO Wiring

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c`
- Modify: `qemu/hw/arm/rcd330.c`
- Modify: `tools/arm_hal_shim/hal_gpio_shim.c`
- Modify: `tools/tests/test_plan2_hardware_gpio_input.py`

**Interfaces:**
- Consumes: Kernel `/dev/v850srq` (wired to `GPIO2_8` in Task 1) and `rcd330-vip` MMIO `0x021fc000`.
- Produces:
  - `tools/arm_hal_shim/hal_gpio_shim.c`:
    - If real `/dev/v850srq` opens cleanly (now backed by `GPIO2_8` IRQ `0x128` in the kernel), let the real kernel driver handle `open("/dev/v850srq")` and `fcntl(F_SETOWN / F_SETFL O_ASYNC)` directly — only fall back to a pipe if `/dev/v850srq` does not exist, and even then only deliver `SIGIO` when `mmio[0x18 / 4]` (`q_count > 0`) is non-zero!
    - Expose `mmio[0x18 / 4]` in `qemu/hw/misc/rcd330_vip.c` returning `(s->q_count > 0 ? 1 : 0) | (s->key_count > 0 ? 2 : 0) | (s->vol_count > 0 ? 4 : 0)` so no unnecessary `SIGIO` signals or MMIO reads occur when queues are empty.

- [ ] **Step 1: Add test for event-driven SRQ status register `0x18` and removal of unconditional `kill(getpid(), SIGIO)` spam**

Append to `TestPlan2HardwareGpioInput` in `tools/tests/test_plan2_hardware_gpio_input.py`:

```python
    def test_vip_exposes_queue_status_and_shim_avoids_unconditional_sigio(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        self.assertIn("offset == 0x18", vip_c)
        self.assertIn("0x18 / 4", shim_c)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan2_hardware_gpio_input.py -v`
Expected: FAIL (`offset == 0x18` not found in `rcd330_vip.c`).

- [ ] **Step 3: Add queue status register `0x18` in `rcd330_vip.c` and gate `hal_gpio_shim.c` on `mmio[0x18 / 4]`**

1. In `qemu/hw/misc/rcd330_vip.c` inside `vip_mmio_read`:
```c
    if (offset == 0x18) {
        uint32_t st = 0;
        if (s->q_count > 0) st |= 1u;
        if (s->key_count > 0) st |= 2u;
        if (s->vol_count > 0) st |= 4u;
        return st;
    }
```
Also, when a host frame is committed in `vip_mmio_write` (`offset == 0x08`), call `vip_pulse_srq(s)` so `spi_ipcl_communication` receives its state-1 ACK phase notification immediately.
2. In `tools/arm_hal_shim/hal_gpio_shim.c`:
   - In `v850srq_sigio_thread`, check `volatile uint32_t *mmio = get_vip_mmio(real_open);` and only call `kill(getpid(), SIGIO)` when `(mmio && (mmio[0x18 / 4] & 1u))` or when an ACK is pending (`g_need_ack_sigio`), eliminating the unconditional 20 Hz `SIGIO` storm when the VIP queue is idle.
   - In `event0_poll_thread`, check `uint32_t st = mmio[0x18 / 4];` first before reading `0x20` or `0x24`.
3. Rebuild QEMU (`./tools/build_qemu.sh`) and rebuild guest shims (`./tools/build_shims.sh --force --update-prebuilt`).

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 tools/tests/test_plan2_hardware_gpio_input.py -v`
Expected: PASS (4/4 tests passing).

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: expose pending queue status bitmask at MMIO 0x18"
git add tools/arm_hal_shim/hal_gpio_shim.c tools/prebuilt/ tools/tests/test_plan2_hardware_gpio_input.py
git commit -m "tools/arm_hal_shim: gate SIGIO and input polling on VIP MMIO 0x18 pending status"
```
