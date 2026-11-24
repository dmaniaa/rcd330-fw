#!/usr/bin/env python3
import pathlib
import subprocess
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan1TarRunner(unittest.TestCase):
    def test_rcd330_c_does_not_shadow_native_usbphy_and_maps_reserved_gap(self):
        rcd330_c = (ROOT / "qemu/hw/arm/rcd330.c").read_text()
        self.assertNotIn("rcd330-usbphy-stub", rcd330_c)
        self.assertNotIn("memory_region_add_subregion(get_system_memory(), 0x020c9000", rcd330_c)
        self.assertIn("0x020cb000", rcd330_c)
        self.assertIn("rcd330-usbphy-rsvd", rcd330_c)

    def test_build_rootfs_synthetic_raw_zimage_readonly_and_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            src_dir = tmp_path / "fw_tree"
            (src_dir / "usr/lib").mkdir(parents=True)
            (src_dir / "opt").mkdir(parents=True)
            (src_dir / "ro_dir").mkdir(parents=True)

            # Raw zImage (not starting with 0x27051956 uImage magic)
            raw_zimg = b"\x00" * 0x24 + b"\x18\x28\x6f\x01" + b"RAW_ZIMAGE_PAYLOAD" * 8
            (src_dir / "kernelImage").write_bytes(raw_zimg)
            # Symlink that Factory Base Layer overlays (must NOT mutate target)
            (src_dir / "usr/lib/libEGL-fb.so").write_bytes(b"ORIGINAL_VIVANTE_EGL")
            (src_dir / "usr/lib/libEGL.so.1").symlink_to("libEGL-fb.so")
            # Installer-only clutter that should be stripped from initramfs
            (src_dir / "u-boot.bin").write_bytes(b"UBOOT_CLUTTER")
            (src_dir / "vipImage.bin").write_bytes(b"VIP_CLUTTER")
            # Read-only file in read-only directory
            ro_file = src_dir / "ro_dir/locked.txt"
            ro_file.write_text("keep")
            ro_file.chmod(0o444)
            (src_dir / "ro_dir").chmod(0o555)

            tar_path = tmp_path / "synthetic_fw.tar"
            with tarfile.open(tar_path, "w") as tf:
                for item in sorted(src_dir.iterdir()):
                    tf.add(item, arcname=item.name)

            # Restore permissions so TemporaryDirectory cleanup succeeds
            (src_dir / "ro_dir").chmod(0o755)
            ro_file.chmod(0o644)

            tar_before = tar_path.read_bytes()
            out_cpio = tmp_path / "out.cpio.gz"
            out_zimg = tmp_path / "out.zImage"

            # Run twice to verify repeated builds over read-only staged dirs succeed
            for _ in range(2):
                subprocess.run(
                    [
                        str(ROOT / "tools/build_rootfs.sh"),
                        "--fw",
                        str(tar_path),
                        "--out-cpio",
                        str(out_cpio),
                        "--out-zimage",
                        str(out_zimg),
                    ],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    check=True,
                )

            self.assertEqual(tar_path.read_bytes(), tar_before)
            self.assertEqual(out_zimg.read_bytes(), raw_zimg)

            # Inspect cpio contents: libEGL-fb.so intact, u-boot.bin stripped
            extract_dir = tmp_path / "verify"
            extract_dir.mkdir()
            subprocess.run(
                f"gzip -dc '{out_cpio}' | (cd '{extract_dir}' && cpio -idmu 2>/dev/null)",
                shell=True,
                check=True,
            )
            self.assertEqual(
                (extract_dir / "usr/lib/libEGL-fb.so").read_bytes(),
                b"ORIGINAL_VIVANTE_EGL",
            )
            self.assertFalse((extract_dir / "usr/lib/libEGL.so.1").is_symlink())
            self.assertFalse((extract_dir / "u-boot.bin").exists())
            self.assertFalse((extract_dir / "vipImage.bin").exists())

            # Missing kernelImage must fail cleanly instead of leaving stale zImage
            no_kern_tar = tmp_path / "no_kernel.tar"
            with tarfile.open(no_kern_tar, "w") as tf:
                tf.add(src_dir / "opt", arcname="opt")
            res = subprocess.run(
                [str(ROOT / "tools/build_rootfs.sh"), "--fw", str(no_kern_tar)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("kernelImage", res.stderr)
            for stale in (ROOT / "build/cache").glob("synthetic_fw_*"):
                subprocess.run(["rm", "-rf", str(stale)], check=False)
            for stale in (ROOT / "build/cache").glob("no_kernel_*"):
                subprocess.run(["rm", "-rf", str(stale)], check=False)

    def test_build_rootfs_from_5606_tar(self):
        tar_5606 = ROOT / "original-fw/std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar"
        if not tar_5606.exists():
            self.skipTest("5606 tar not present")
        res = subprocess.run(
            [str(ROOT / "tools/build_rootfs.sh"), "--fw", str(tar_5606)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertTrue((ROOT / "build/factory_base.cpio.gz").exists())
        zimg = ROOT / "build/cache/std_plus_pss-6.5inch_5606_SKODA_DE_CV/zImage"
        cpio = ROOT / "build/cache/std_plus_pss-6.5inch_5606_SKODA_DE_CV/rootfs.cpio.gz"
        self.assertTrue(zimg.exists(), res.stdout + res.stderr)
        self.assertTrue(cpio.exists(), res.stdout + res.stderr)
        # Verify zImage has ARM Linux zImage magic 0x016f2818 at offset 0x24
        hdr = zimg.read_bytes()[:0x30]
        self.assertEqual(hdr[0x24:0x28], b"\x18\x28\x6f\x01")

    def test_run_rcd330_dry_run_switches_firmware_and_checks_staleness(self):
        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        shims_sh = (ROOT / "tools/build_shims.sh").read_text()
        self.assertIn("-nt", run_sh)
        self.assertNotIn("extracted-fw", shims_sh)

        tar_5624 = ROOT / "original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar"
        if not tar_5624.exists():
            self.skipTest("5624 tar not present")
        res = subprocess.run(
            [str(ROOT / "tools/run_rcd330.sh"), "--dry-run", "--fw", str(tar_5624)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn("-M rcd330", res.stdout)
        self.assertIn("zImage", res.stdout)
        self.assertIn("rootfs", res.stdout)


if __name__ == "__main__":
    unittest.main()
