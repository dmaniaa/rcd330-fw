#!/usr/bin/env python3
"""ABI gate for the recreated opt/hmi (build/hmi/hmi).

The binary must load on the device's own libraries: ARMv7 softfp EXEC with
the stock dynamic linker, a DT_NEEDED subset of stock opt/hmi, and no symbol
version newer than the device's glibc 2.13 / libstdc++ 6.0.16 (GCC 4.6).

Skipped when build/hmi/hmi is missing, unless RCD330_REQUIRE_HMI=1.
RCD330_HMI overrides the binary under test (tools/build_hmi.sh gates the
freshly linked build/hmi/hmi.new before moving it into place).
"""
import functools
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
HMI = pathlib.Path(os.environ.get("RCD330_HMI") or ROOT / "build/hmi/hmi").resolve()
MOC = ROOT / "build/hmi/host-tools/moc"
STOCK_HMI = ROOT / "extracted-fw/5624_vw/opt/hmi"
STOCK_OPT = ROOT / "extracted-fw/5624_vw/opt"
SYSROOT_QTCORE = ROOT / "build/cache/std_plus_pss-6.5inch_5624_VW_PQ_ROW/stage/usr/lib/libQtCore.so.4"

EF_ARM_ABI_FLOAT_HARD = 0x400

MAX_VERSIONS = {
    "GLIBCXX": (3, 4, 16),
    "CXXABI": (1, 3, 5),
    "GLIBC": (2, 13),
}


@functools.lru_cache(maxsize=None)
def find_llvm_readelf():
    candidates = [os.environ.get("LLVM_READELF"), shutil.which("llvm-readelf")]
    candidates += [
        "/opt/homebrew/opt/llvm/bin/llvm-readelf",
        "/usr/local/opt/llvm/bin/llvm-readelf",
    ]
    candidates += sorted(map(str, pathlib.Path("/usr/lib").glob("llvm-*/bin/llvm-readelf")))
    for cand in candidates:
        if cand and os.access(cand, os.X_OK):
            return cand
    return None


def readelf(*args):
    tool = find_llvm_readelf()
    if tool is None:
        raise AssertionError("llvm-readelf not found (set LLVM_READELF or install llvm)")
    return subprocess.check_output([tool, *args], text=True)


def needed_libs(path):
    return set(re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", readelf("-d", str(path))))


def dynamic_symbols(path, undefined):
    """Mangled dynamic symbol names (version suffix stripped), UND or defined."""
    names = set()
    for line in readelf("--dyn-syms", "--wide", str(path)).splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].endswith(":"):
            continue
        if (fields[6] == "UND") == undefined:
            names.add(fields[7].split("@")[0])
    names.discard("")
    return names


def copy_relocated_symbols(path):
    """Data symbols bound by R_ARM_COPY: imports too, though dynsym lists them as defined.

    Non-PIC stock code (e.g. opt/hmi writing QTextCodec::cftr) gets a copy of the
    library's variable in its own .bss instead of an UND entry.
    """
    names = set()
    for line in readelf("-r", "--wide", str(path)).splitlines():
        fields = line.split()
        if len(fields) >= 5 and fields[2] == "R_ARM_COPY":
            names.add(fields[4].split("@")[0])
    return names


def stock_qt_imports():
    """Qt symbols the stock 5624 binaries import: the only ones known to exist on the device."""
    imports, users = set(), []
    for path in sorted(STOCK_OPT.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as f:
            if f.read(4) != b"\x7fELF":
                continue
        if "libQtCore.so.4" in needed_libs(path):
            users.append(path)
            imports |= dynamic_symbols(path, undefined=True)
            imports |= copy_relocated_symbols(path)
    return imports, users


class TestHmiReadelfParsing(unittest.TestCase):
    """Guards the parsers the ABI gate relies on: a silent parse failure would weaken it."""

    def test_copy_relocations_parsed_from_stock(self):
        if not STOCK_HMI.is_file():
            self.skipTest(f"{STOCK_HMI} missing; run tools/build_rootfs.sh")
        names = copy_relocated_symbols(STOCK_HMI)
        # Stock opt/hmi carries 20 R_ARM_COPY relocations (QTextCodec::cftr, ...).
        self.assertEqual(len(names), 20, sorted(names))
        self.assertIn("_ZN10QTextCodec4cftrE", names)


class TestHmiBuild(unittest.TestCase):
    def setUp(self):
        if not HMI.is_file():
            if os.environ.get("RCD330_REQUIRE_HMI") == "1":
                self.fail(f"{HMI} missing; run tools/build_hmi.sh")
            self.skipTest(f"{HMI} not built")

    def test_is_arm_elf_exec(self):
        out = readelf("-h", "-l", str(HMI))
        self.assertRegex(out, r"Machine:\s+ARM\b")
        self.assertRegex(out, r"Type:\s+EXEC\b")
        self.assertIn("[Requesting program interpreter: /lib/ld-linux.so.3]", out)

    def test_needed_subset_of_stock(self):
        if not STOCK_HMI.is_file():
            self.fail(f"{STOCK_HMI} missing; run tools/build_rootfs.sh")
        ours = needed_libs(HMI)
        self.assertTrue(ours, "no DT_NEEDED entries found")
        extra = ours - needed_libs(STOCK_HMI)
        self.assertFalse(extra, f"DT_NEEDED not in stock opt/hmi: {sorted(extra)}")

    def test_qt_imports_subset_of_stock(self):
        # The sysroot libQtCore.so.4 is a Qt 4.8.2 stand-in; the device runs Qt
        # Embedded 4.7.4, so only Qt symbols stock binaries import are safe.
        for path in (STOCK_HMI, SYSROOT_QTCORE):
            if not path.is_file():
                self.fail(f"{path} missing; run tools/build_rootfs.sh")
        # Copy-relocated data symbols are imports too (defined in our dynsym).
        ours = dynamic_symbols(HMI, undefined=True) | copy_relocated_symbols(HMI)
        qt_used = ours & dynamic_symbols(SYSROOT_QTCORE, undefined=False)
        self.assertTrue(qt_used, f"no Qt imports found in {HMI} (symbol parsing broken?)")
        allowed, users = stock_qt_imports()
        self.assertIn(STOCK_HMI, users, "stock opt/hmi not recognised as a libQtCore user")
        extra = sorted(qt_used - allowed)
        self.assertFalse(extra, "Qt symbols no stock 5624 binary imports (may be missing on the device): " + ", ".join(extra))

    def test_no_host_rpath(self):
        dynamic = readelf("-d", str(HMI))
        self.assertNotRegex(dynamic, r"\((RPATH|RUNPATH)\)", "host library path baked into the binary")

    def test_symbol_versions_within_device(self):
        versions = set(re.findall(r"\b(GLIBCXX|CXXABI|GLIBC)_([0-9][0-9.]*)", readelf("-V", str(HMI))))
        self.assertTrue(versions, "no versioned symbols found")
        for name, ver in sorted(versions):
            parsed = tuple(int(part) for part in ver.rstrip(".").split("."))
            self.assertLessEqual(parsed, MAX_VERSIONS[name], f"{name}_{ver} is newer than the device provides")
        dynsyms = readelf("--dyn-syms", str(HMI))
        self.assertNotRegex(dynsyms, r"St3__1|std::__1::", "libc++ symbols present")

    def test_cpu_arch_v7(self):
        attrs = readelf("-A", str(HMI))
        match = re.search(r"TagName: CPU_arch\n\s+Description: (.+)", attrs)
        self.assertIsNotNone(match, "no Tag_CPU_arch attribute")
        self.assertEqual(match.group(1).strip(), "ARM v7")

    def test_float_abi_softfp(self):
        # llvm-readelf prints the tag as "TagName: ABI_VFP_args"; hard-float sets it.
        self.assertNotIn("ABI_VFP_args", readelf("-A", str(HMI)))
        # lld may drop that attribute, so also reject EF_ARM_ABI_FLOAT_HARD.
        flags = int(re.search(r"Flags:\s+(0x[0-9a-fA-F]+)", readelf("-h", str(HMI))).group(1), 16)
        self.assertFalse(flags & EF_ARM_ABI_FLOAT_HARD, f"hard-float ELF flags {flags:#x}")


class TestHmiTrace(unittest.TestCase):
    """Origin-tag index (src/hmi/TRACE.json) written by src/hmi/tools/gen_trace.py."""
    GEN_TRACE = ROOT / "src/hmi/tools/gen_trace.py"

    def run_gen_trace(self, *args):
        return subprocess.run(["python3", str(self.GEN_TRACE), *args], capture_output=True, text=True)

    def test_trace_index_current(self):
        result = self.run_gen_trace("--check")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        trace = json.loads((ROOT / "src/hmi/TRACE.json").read_text())
        self.assertEqual(trace["_coverage"]["total"], 9580)
        symbols = json.loads((ROOT / "docs/decompiled/5624/hmi/hmi_symbols_5624.json").read_text())
        func_addrs = set(symbols.get("function_addresses", []))
        self.assertEqual(len(func_addrs), 9580)
        functions = ROOT / "docs/decompiled/5624/hmi/functions"
        if functions.is_dir():
            disk_addrs = {p.stem for p in functions.glob("0x*.c")}
            self.assertEqual(func_addrs, disk_addrs, "hmi_symbols_5624.json function_addresses drifted")
        recreated = 0
        for va, entry in trace.items():
            if va.startswith("_"):
                continue
            recreated += 1
            key = f"0x{int(va, 16):x}"
            self.assertIn(key, func_addrs, f"{va} {entry['name']} not in stock function_addresses")
            if functions.is_dir():
                self.assertTrue((functions / f"{key}.c").is_file(), f"{va} {entry['name']} has no decompiled file")
        self.assertEqual(recreated, trace["_coverage"]["recreated"])
        self.assertGreater(recreated, 0)

    def test_check_rejects_unknown_va(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "src"
            src.mkdir()
            # 0x00010e57 is inside no function and no data section.
            (src / "a.cpp").write_text("// 0x00010e57 Bogus\nvoid f();\n")
            result = self.run_gen_trace("--check", "--src", str(src), "--out", str(pathlib.Path(tmp) / "T.json"))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("0x00010e57", result.stdout + result.stderr)

    def test_check_detects_stale_index(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "src"
            src.mkdir()
            (src / "a.cpp").write_text("// 0x00010e58 main\nint main();\n")
            out = pathlib.Path(tmp) / "T.json"
            self.assertEqual(self.run_gen_trace("--src", str(src), "--out", str(out)).returncode, 0)
            self.assertEqual(self.run_gen_trace("--check", "--src", str(src), "--out", str(out)).returncode, 0)
            (src / "a.cpp").write_text("\n// 0x00010e58 main\nint main();\n")
            self.assertNotEqual(self.run_gen_trace("--check", "--src", str(src), "--out", str(out)).returncode, 0)


class TestHmiMoc(unittest.TestCase):
    def setUp(self):
        if not MOC.is_file():
            if os.environ.get("RCD330_REQUIRE_HMI") == "1":
                self.fail(f"{MOC} missing; run tools/build_hmi.sh")
            self.skipTest(f"{MOC} not built")

    def test_moc_revision_62(self):
        ver = subprocess.run([str(MOC), "-v"], capture_output=True, text=True)
        combined = ver.stdout + ver.stderr
        self.assertEqual(ver.returncode, 1, combined)
        self.assertIn("4.7.4", combined)
        self.assertIn("62", combined)
        sample = (
            "#include <QtCore/QObject>\n"
            "class ProbeObj : public QObject {\n"
            "    Q_OBJECT\n"
            "signals:\n"
            "    void ping(int v);\n"
            "public slots:\n"
            "    void pong(int v);\n"
            "};\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            hdr = pathlib.Path(tmp) / "probe.h"
            hdr.write_text(sample)
            out = subprocess.check_output(
                [str(MOC), "-DQT_NO_DEBUG", str(hdr)], text=True
            )
        self.assertIn("cannot be used with the include files", out)
        self.assertIn("62", out)
        self.assertIn("QMetaObject::activate", out)
        self.assertNotIn("qt_static_metacall", out)


if __name__ == "__main__":
    unittest.main()
