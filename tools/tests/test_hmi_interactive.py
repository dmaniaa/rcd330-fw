#!/usr/bin/env python3
"""End-to-end interactive QEMU test for the Phase 2 recreated opt/hmi.

One boot of build/hmi/hmi, then real input only:

  boot          -> tuner main page 0xf9 (ShowPage 249) + first frame and
                   pwr_agent's wake-state cycle (Rcd330Qemu.wait_hmi_ready)
  BTN APP       -> AppNoConn 0x04 (ShowPage 4)
  touch Info    -> AppConnectInfo 0x03 (ShowPage 3)   [VBtnInfo, QMP touch]
  touch Back    -> 0x03 removed, AppNoConn back on screen (frame compare)
  BTN RADIO     -> VDisplayProxy::InsertPage 1: ForceShowPage keeps the
                   tuner page on layer 0 and removes 0x04 (no ShowPage log,
                   as stock 0x0002fc28), so the frame must match the boot one
  ENC TUNE +1/-1 -> VIPCLNotifyFilter HandleLPCLHardKeyTuneUp / ...Down
  BTN MEDIA, BTN PHONE, BTN SETUP (twice each, Phase 3) -> one
                   "not implemented" warning each, no crash, no permit
                   corruption
  BTN APP + touch Info again -> still responsive (review focus 5)

Every expectation is searched only in serial output produced after the
input that should cause it (Rcd330Qemu.mark / wait_serial(since=...)), so a
line from an earlier step can never satisfy a later one. At the end the hmi
must have started exactly once (no pwr_agent restart) and never segfaulted.

On failure the whole serial log is left in build/hmi/interactive_serial.txt.

Skipped unless RCD330_QEMU_TESTS=1.
"""
import os
import pathlib
import re
import sys
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools/tests"))

from hmi_qemu import Rcd330Qemu  # noqa: E402
from test_hmi_parity import compare, build_mask, read_ppm, TOLERANCE  # noqa: E402

HMI = ROOT / "build/hmi/hmi"
OUT_DIR = ROOT / "build/hmi"
FAIL_SERIAL = OUT_DIR / "interactive_serial.txt"
STEP_TIMEOUT_S = 30
SETTLE_S = 1.5
# Info button (VBtnInfo, touch tag 0x40100167 at (0, 0x18c) 800x0x54 on 0x04)
# and Back (touch tag 0x4010015e at (0x2cf, 1) 0x3d x 0x3d on 0x03).
INFO_XY = (400, 438)
BACK_XY = (0x2cf + 0x3d // 2, 1 + 0x3d // 2)
PHASE3_WARN = r"WARN: HandleLPCLHardKey%s \(key \d+\) is not implemented in phase 2"
SAME_FRAME_MIN = 0.995


@unittest.skipUnless(os.environ.get("RCD330_QEMU_TESTS") == "1", "set RCD330_QEMU_TESTS=1")
class HmiInteractiveTest(unittest.TestCase):
    def press(self, qemu, name):
        since = qemu.mark()
        qemu.vip(f"BTN {name} 1")
        time.sleep(0.3)
        qemu.vip(f"BTN {name} 0")
        return since

    def tap(self, qemu, xy):
        since = qemu.mark()
        qemu.touch(*xy)
        return since

    def assert_same_frame(self, a_path, b_path, what):
        _, _, a = read_ppm(a_path)
        _, _, b = read_ppm(b_path)
        match, compared, _ = compare(a, b, build_mask([]))
        ratio = match / compared
        print(f"\n{what}: {match}/{compared} pixels within +-{TOLERANCE} ({ratio:.4%})")
        self.assertEqual(compared, 800 * 480)
        self.assertGreaterEqual(ratio, SAME_FRAME_MIN, f"{what}: frames differ ({ratio:.4%})")

    def test_boot_hardkeys_touch_knob_and_phase3_safety(self):
        self.assertTrue(HMI.is_file(), f"{HMI} missing; run tools/build_hmi.sh")
        tuner_ppm = OUT_DIR / "interactive_tuner.ppm"
        noconn_ppm = OUT_DIR / "interactive_appnoconn.ppm"
        back_ppm = OUT_DIR / "interactive_after_back.ppm"
        radio_ppm = OUT_DIR / "interactive_after_radio.ppm"
        with Rcd330Qemu(hmi=str(HMI)) as qemu:
            try:
                qemu.wait_hmi_ready()
                time.sleep(SETTLE_S)
                qemu.screendump_stable(tuner_ppm)

                # Hardkey APP -> AppNoConn (0x04).
                since = self.press(qemu, "APP")
                qemu.wait_serial(r"VPageManager::ShowPage 4\b", STEP_TIMEOUT_S, since=since)
                time.sleep(SETTLE_S)
                qemu.screendump_stable(noconn_ppm)

                # Touch VBtnInfo -> AppConnectInfo (0x03).
                since = self.tap(qemu, INFO_XY)
                qemu.wait_serial(r"VPageManager::ShowPage 3\b", STEP_TIMEOUT_S, since=since)
                time.sleep(SETTLE_S)

                # Touch Back -> VDisplayProxy::RemovePage(3): 0x04 is on
                # screen again (stock logs nothing for the close, so the
                # frame must match the AppNoConn frame from before).
                since = self.tap(qemu, BACK_XY)
                time.sleep(SETTLE_S)
                qemu.screendump_stable(back_ppm)
                self.assert_same_frame(noconn_ppm, back_ppm, "AppNoConn before Info vs after Back")
                self.assertNotRegex(qemu.serial_text()[since:], r"VPageManager::ShowPage \d+",
                                    "Back opened a page instead of closing 0x03")

                # Hardkey RADIO -> tuner main page (0xf9) back on top.
                since = self.press(qemu, "RADIO")
                qemu.wait_serial(r"VDisplayProxy::InsertPage 1,", STEP_TIMEOUT_S, since=since)
                time.sleep(SETTLE_S)
                qemu.screendump_stable(radio_ppm)
                self.assert_same_frame(tuner_ppm, radio_ppm, "Tuner after boot vs after RADIO")

                # Tune rotary encoder (+1 CW, -1 CCW) reaches VIPCLNotifyFilter.
                since = qemu.mark()
                qemu.knob(1)
                qemu.wait_serial(r"HandleLPCLHardKeyTuneUp", STEP_TIMEOUT_S, since=since)
                since = qemu.mark()
                qemu.knob(-1)
                qemu.wait_serial(r"HandleLPCLHardKeyTuneDown", STEP_TIMEOUT_S, since=since)

                # Review focus 5: Phase-3 hardkeys log once, never crash, and
                # leave APP / touch responsive.
                phase3_since = qemu.mark()
                for name in ("MEDIA", "PHONE", "SETUP", "MEDIA", "PHONE", "SETUP"):
                    self.press(qemu, name)
                    time.sleep(0.5)
                qemu.wait_serial(PHASE3_WARN % "Media", STEP_TIMEOUT_S, since=phase3_since)
                qemu.wait_serial(PHASE3_WARN % "Phone", STEP_TIMEOUT_S, since=phase3_since)
                qemu.wait_serial(PHASE3_WARN % "Set", STEP_TIMEOUT_S, since=phase3_since)

                since = self.press(qemu, "APP")
                qemu.wait_serial(r"VPageManager::ShowPage 4\b", STEP_TIMEOUT_S, since=since)
                time.sleep(SETTLE_S)
                since = self.tap(qemu, INFO_XY)
                qemu.wait_serial(r"VPageManager::ShowPage 3\b", STEP_TIMEOUT_S, since=since)
                time.sleep(SETTLE_S)
                text = qemu.serial_text()
            except BaseException:
                FAIL_SERIAL.parent.mkdir(parents=True, exist_ok=True)
                FAIL_SERIAL.write_text(qemu.serial_text())
                raise

        phase3 = text[phase3_since:]
        for name in ("Media", "Phone", "Set"):
            self.assertEqual(len(re.findall(PHASE3_WARN % name, phase3)), 1,
                             f"{name} key warning not logged exactly once")
        self.assertEqual(text.count("[HMI_START]HMI main start"), 1,
                         "hmi was (re)started more than once (pwr_agent restart)")
        self.assertNotIn("Segmentation fault", text)
        FAIL_SERIAL.unlink(missing_ok=True)
        for ppm in (tuner_ppm, noconn_ppm, back_ppm, radio_ppm):
            ppm.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
