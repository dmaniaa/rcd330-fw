#!/usr/bin/env python3
"""QEMU boot test for the recreated opt/hmi (build/hmi/hmi) under pwr_agent.

pwr_agent restarts hmi whenever it exits, so a crashing or returning main()
shows up as repeated start banners. Each test boots the stock rootfs (with
our binary over /opt/hmi, or stock), waits for the banner, then checks that
after 60 s it was printed exactly once and nothing segfaulted. The
recreated hmi must also show the tuner main page and swap its first frame;
if it does not, the full serial log is left in build/hmi/boot_fail_serial.txt.

Slow (~4 min per boot): skipped unless RCD330_QEMU_TESTS=1.
"""
import os
import pathlib
import sys
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from hmi_qemu import Rcd330Qemu  # noqa: E402

BANNER = r"\[HMI_START\]HMI main start"
BANNER_TIMEOUT_S = 180
STAY_UP_S = 60
BOOT_PAGE = r"VPageManager::ShowPage 249[\s\S]*gl_fb_shim: eglSwapBuffers #1"
BOOT_PAGE_TIMEOUT_S = 120
FAIL_SERIAL = ROOT / "build/hmi/boot_fail_serial.txt"


@unittest.skipUnless(os.environ.get("RCD330_QEMU_TESTS") == "1", "set RCD330_QEMU_TESTS=1 to run QEMU boot tests")
class HmiBootTest(unittest.TestCase):
    def assert_boots_and_stays_up(self, hmi):
        with Rcd330Qemu(hmi=hmi) as qemu:
            qemu.wait_serial(BANNER, BANNER_TIMEOUT_S)
            time.sleep(STAY_UP_S)
            text = qemu.serial_text()
        self.assertEqual(text.count("[HMI_START]HMI main start"), 1,
                         "hmi was (re)started more than once; serial tail:\n" + text[-3000:])
        self.assertNotIn("Segmentation fault", text)

    def test_recreated_hmi_boots_and_stays_up(self):
        hmi = ROOT / "build/hmi/hmi"
        self.assertTrue(hmi.is_file(), f"{hmi} missing; run tools/build_hmi.sh")
        with Rcd330Qemu(hmi=str(hmi)) as qemu:
            qemu.wait_serial(BANNER, BANNER_TIMEOUT_S)
            # Like stock: the VMF handshake leads to the tuner main page
            # (screen 0xf9 = 249) and its first frame reaches the display.
            try:
                qemu.wait_serial(BOOT_PAGE, BOOT_PAGE_TIMEOUT_S)
            except AssertionError:
                # The tail is usually clear-cache.sh noise: keep the whole log.
                FAIL_SERIAL.parent.mkdir(parents=True, exist_ok=True)
                FAIL_SERIAL.write_text(qemu.serial_text())
                raise
            time.sleep(STAY_UP_S)
            text = qemu.serial_text()
        self.assertEqual(text.count("[HMI_START]HMI main start"), 1,
                         "hmi was (re)started more than once; serial tail:\n" + text[-3000:])
        self.assertNotIn("Segmentation fault", text)

    def test_stock_hmi_still_boots(self):
        self.assert_boots_and_stays_up(None)


if __name__ == "__main__":
    unittest.main()
