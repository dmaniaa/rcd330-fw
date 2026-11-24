#!/usr/bin/env python3
"""QEMU boot test for the recreated opt/hmi (build/hmi/hmi) under pwr_agent.

pwr_agent restarts hmi whenever it exits, so a crashing or returning main()
shows up as repeated start banners. Each test boots the stock rootfs (with
our binary over /opt/hmi, or stock), waits for the banner, then checks that
after 60 s it was printed exactly once and nothing segfaulted.

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
        self.assert_boots_and_stays_up(str(hmi))

    def test_stock_hmi_still_boots(self):
        self.assert_boots_and_stays_up(None)


if __name__ == "__main__":
    unittest.main()
