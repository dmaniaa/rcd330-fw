#!/usr/bin/env python3
import io
import pathlib
import socket
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


class TestPlan3RcsBootAndTuner(unittest.TestCase):
    def test_vip_implements_amfm_tuner_and_watchdog_handshake(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        self.assertIn("vip_send_tuner_status", vip_c)
        self.assertIn("vip_send_station_list", vip_c)
        self.assertIn("grp == 0x05 && sub == 0x01", vip_c)
        self.assertNotIn("grp == 0x04 && sub == 0x01", vip_c)
        self.assertIn("pl[1] & 1", vip_c)
        self.assertIn("pl[0] == 0x30", vip_c)
        self.assertIn("pl[0] == 0x4e", vip_c)
        self.assertNotIn("static uint8_t pwr_off", vip_c)
        self.assertIn("TUNER", vip_c)

    def test_native_gui_exposes_tuner_controls_and_hardens_vip_protocol(self):
        vehicle_cpp = (ROOT / "tools/rcd330_gui/src/vehicle_tab.cpp").read_text()
        vip_cpp = (ROOT / "tools/rcd330_gui/src/vip_client.cpp").read_text()
        self.assertIn("TUNER 9580 RCD 330", vehicle_cpp)
        self.assertIn("HOST_TX ", vip_cpp)
        self.assertIn("cmd.contains('\\n')", vip_cpp)
        self.assertIn("cmd.contains('\\r')", vip_cpp)

    def test_rootfs_uses_unmodified_rcS_and_safe_mount_wrapper(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        init_c = (ROOT / "tools/arm_init/init.c").read_text()
        self.assertNotIn("rc.qemu", build_sh)
        self.assertNotIn("vmf_send 16 1 1", build_sh)
        self.assertIn("/etc/rc.d/rcS", init_c)
        self.assertNotIn("rc.qemu", init_c)
        # /bin/mount wrapper must check /proc/mounts rather than blindly skipping /proc /sys /tmp
        self.assertIn("/proc/mounts", build_sh)
        self.assertNotIn('*"/proc"*|*"/sys"*|*"/tmp"*', build_sh)

    def test_legacy_launch_paths_removed_and_positional_tar_works(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertNotIn("extracted-fw", build_sh)
        self.assertNotIn("extracted-fw", run_sh)
        self.assertNotIn("--with-ext2", build_sh)
        self.assertNotIn("mke2fs", build_sh)
        tar_5606 = ROOT / "original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar"
        if tar_5606.exists():
            res = subprocess.run(
                [str(ROOT / "tools/run_rcd330.sh"), str(tar_5606), "--dry-run"],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertIn("std_plus_pss-6.5inch_5606_SKODA_DE_CV", res.stdout)


if __name__ == "__main__":
    unittest.main()
