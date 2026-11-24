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

    def test_vip_and_atmel_mxt_use_resettable_exit_phase_for_idle_high_irqs(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        mxt_c = (ROOT / "qemu/hw/input/atmel_mxt.c").read_text()
        self.assertIn("phases.exit", vip_c)
        self.assertIn("phases.exit", mxt_c)
        self.assertIn("srq_low", vip_c)

    def test_vip_enc_tune_does_not_lock_out_keys_or_double_step(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        enc_section = vip_c.split('!strcmp(arg1, "TUNE")')[1].split("} else {")[0]
        self.assertNotIn("vip_enqueue_key", enc_section)
        self.assertIn("knob_timer", vip_c)
        self.assertIn("knob_pending_steps", vip_c)

    def test_atmel_mxt_handles_factory_pointercal_reportall_and_rounding(self):
        mxt_c = (ROOT / "qemu/hw/input/atmel_mxt.c").read_text()
        self.assertIn("mxt_screen_to_raw", mxt_c)
        self.assertIn("mxt_enforce_t100_range", mxt_c)
        self.assertIn("MXT_T6_ADDR + 3", mxt_c)
        self.assertIn("div_round_nearest", mxt_c)
        # Verify round-to-nearest inverse math against factory /etc/pointercal
        a0, a1, a2 = 775, -66330, 53052216
        a3, a4, a5, a6 = -67001, -250, 31844040, 65536
        det = a0 * a4 - a1 * a3

        def div_round(num, den):
            if den < 0:
                num, den = -num, -den
            return (num + den // 2) // den if num >= 0 else -((-num + den // 2) // den)

        for sx, sy in [(400, 240), (799, 0), (799, 479), (10, 32), (10, 479)]:
            rhs_x = sx * a6 - a2
            rhs_y = sy * a6 - a5
            mt_x = div_round(rhs_x * a4 - rhs_y * a1, det)
            mt_y = div_round(a0 * rhs_y - a3 * rhs_x, det)
            rx = max(0, min(799, 799 - mt_y))
            ry = max(0, min(479, 479 - mt_x))
            # Forward transform: kernel maps (rx, ry) -> mt_x=479-ry, mt_y=799-rx, then tslib pointercal
            f_mtx = 479 - ry
            f_mty = 799 - rx
            out_x = (a0 * f_mtx + a1 * f_mty + a2) // a6
            out_y = (a3 * f_mtx + a4 * f_mty + a5) // a6
            self.assertLessEqual(abs(out_x - sx), 1, f"({sx},{sy}) -> ({out_x},{out_y})")
            self.assertLessEqual(abs(out_y - sy), 1, f"({sx},{sy}) -> ({out_x},{out_y})")

    def test_build_rootfs_preserves_factory_pointercal_and_maxtouch_cfg(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        self.assertNotIn('rm -f "$STAGE_DIR/lib/firmware/maxtouch.cfg"', build_sh)
        self.assertNotIn('> "$STAGE_DIR/etc/pointercal"', build_sh)

    def test_vip_exposes_queue_status_and_shim_drains_tx_without_ack_lock(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        self.assertIn("offset == 0x18", vip_c)
        self.assertIn("0x18 / 4", shim_c)
        self.assertIn("msgsnd(", shim_c)
        self.assertIn("IPC_STAT", shim_c)
        self.assertIn("__atomic_", shim_c)
        self.assertNotIn("g_need_ack_sigio = 2", shim_c)
        self.assertIn('real_open("/dev/v850srq"', shim_c)


if __name__ == "__main__":
    unittest.main()
