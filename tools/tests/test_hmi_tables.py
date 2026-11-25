#!/usr/bin/env python3
"""IGDT table generator (src/hmi/tools/gen_igdt_tables.py) against stock 5624.

The generator follows the decompiled stock walker. The 14 screens in
resources/scenes/scenes_5624_vw.json (an earlier extractor) must match it
except for the extractor defects listed in EXTRACTOR_EXCEPTIONS.

Firmware-dependent tests are skipped when extracted-fw/5624_vw/opt/hmi is
missing, unless RCD330_REQUIRE_HMI=1.
"""
import collections
import difflib
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "src/hmi/tools"
GEN = TOOLS / "gen_igdt_tables.py"
STOCK_HMI = ROOT / "extracted-fw/5624_vw/opt/hmi"
STOCK_OPT = ROOT / "extracted-fw/5624_vw/opt"
SCENES_JSON = ROOT / "resources/scenes/scenes_5624_vw.json"
STRINGS_JSON = ROOT / "resources/translations/strings_5624_vw.json"
STOCK_SHA256 = "44ccad4211fc8f2fb21f2db46a04271155b8d894735427802baee7467630a6f2"

sys.path.insert(0, str(TOOLS))

DEPTH_CAP = "depth cap 12"
KIND7 = "kind 7 skipped"
BRANCH_BY_TAG = "branch-by-tag"

# Nesting level below which the extractor stopped walking.
EXTRACTOR_MAX_DEPTH = 12

# Where stock-faithful output may differ from scenes_5624_vw.json:
# screen -> {(op, extractor defect, detail): element count}. op: "extra" =
# only in ours, "missing" = only in the JSON (detail = its tag), "changed" =
# same position, other node (detail = "JSON tag->our tag").
#   depth cap 12:   the extractor stopped walking below nesting level 12.
#   kind 7 skipped: the extractor ignored static kind-7 rect nodes.
#   branch-by-tag:  the extractor indexed branch_init by tag & 0xfffff;
#                   FUN_00321ea8 indexes it by node eidx.
EXTRACTOR_EXCEPTIONS = {
    0x0a: {("extra", DEPTH_CAP, None): 34},
    # Branch 0x41000025 (eidx 0x3b4): branch_init[0x3b4] = 0 -> dial without glow, tag index 0x25 -> with glow img.
    0x17: {("missing", BRANCH_BY_TAG, "0x02100034"): 1},
    # Branch 0x41000054 (eidx 0x756): branch_init[0x756] = 0 -> icon_0, tag index 0x54 -> icon_1.
    0x52: {("changed", BRANCH_BY_TAG, "0x421032bb->0x421032ba"): 1},
    0x63: {("extra", DEPTH_CAP, None): 13},
    0xdf: {("extra", KIND7, None): 1},
    0xfe: {("extra", DEPTH_CAP, None): 4},
}

# Elements below nesting level 12 per screen (the extractor's cut-off).
DEEP_ELEMENTS = {0x0a: 34, 0x0d: 18, 0x58: 40, 0x60: 162, 0x63: 13, 0xa5: 36, 0xfe: 4}
RECT_ELEMENTS = {0x01: 1, 0x1d: 1, 0x46: 2, 0xdf: 1}


def extra_reason(element):
    if element.depth > EXTRACTOR_MAX_DEPTH:
        return DEPTH_CAP
    if element.kind == 7:
        return KIND7
    return "unexplained"


def oracle_diff(want, elements):
    """{(op, reason): count} for the JSON elements vs our Element list."""
    a = [json.dumps(e, sort_keys=True) for e in want]
    b = [json.dumps(e.as_json(), sort_keys=True) for e in elements]
    tally = collections.Counter()
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if op == "insert":
            tally.update(("extra", extra_reason(e), None) for e in elements[j1:j2])
        elif op == "delete":
            tally.update(("missing", BRANCH_BY_TAG, e["tag"]) for e in want[i1:i2])
        elif op == "replace":
            if i2 - i1 != j2 - j1:
                tally[("replace", "unexplained", None)] += max(i2 - i1, j2 - j1)
            else:
                tally.update(("changed", BRANCH_BY_TAG, "%s->0x%08x" % (w["tag"], e.tag))
                             for w, e in zip(want[i1:i2], elements[j1:j2]))
    return dict(tally)


def run_gen(*args):
    return subprocess.run([sys.executable, str(GEN), *args],
                          capture_output=True, text=True, cwd=ROOT)


class TestHmiTables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.elf = None
        if not STOCK_HMI.is_file():
            return
        import gen_igdt_tables
        import hmi_elf
        cls.gen = gen_igdt_tables
        cls.elf = hmi_elf.Elf(str(STOCK_HMI))

    def require_firmware(self):
        if self.elf is None:
            if os.environ.get("RCD330_REQUIRE_HMI") == "1":
                self.fail(f"{STOCK_HMI} missing; run tools/build_rootfs.sh")
            self.skipTest(f"{STOCK_HMI} missing")

    def test_screens_match_extracted_json(self):
        self.require_firmware()
        oracle = json.loads(SCENES_JSON.read_text())["screens"]
        self.assertEqual(len(oracle), 14)
        screens = self.gen.load_screens(self.elf)
        raw = self.gen.load_screen_elements(self.elf)
        for key, want in oracle.items():
            sid = int(key, 16)
            with self.subTest(screen=key):
                got = screens[sid]
                for field in ("screen_id", "name", "layer_group", "root_count"):
                    self.assertEqual(got[field], want[field], field)
                self.assertEqual(got["element_count"], len(got["elements"]))
                self.assertEqual(oracle_diff(want["elements"], raw[sid][2]),
                                 EXTRACTOR_EXCEPTIONS.get(sid, {}))
                if sid not in EXTRACTOR_EXCEPTIONS:
                    self.assertEqual(got["elements"], want["elements"])

    def test_uncapped_walk_reaches_deep_elements(self):
        self.require_firmware()
        raw = self.gen.load_screen_elements(self.elf)
        deep = {sid: n for sid, n in
                ((sid, sum(1 for e in v[2] if e.depth > EXTRACTOR_MAX_DEPTH))
                 for sid, v in raw.items()) if n}
        self.assertEqual(deep, DEEP_ELEMENTS)
        self.assertEqual(sum(deep.values()), 307)

    def test_group_clip_carried_by_elements(self):
        self.require_firmware()
        raw = self.gen.load_screen_elements(self.elf)
        # MediaOverview progress bar: dyn group 0x4010158f (eidx 0xea8,
        # flags 0x1e) clips with clip_table[3741] = (128, 348, 542, 15).
        bar = [e for e in raw[0x52][2] if e.tag in (0x421032b7, 0x421032b8, 0x421032b9)]
        self.assertEqual(len(bar), 3)
        for e in bar:
            self.assertEqual(e.clip, (128, 348, 670, 363))
        self.assertEqual(self.elf.unpack("<BBH", 0x454694 + 0xea8 * 4), (0x1e, 0, 3741))
        self.assertEqual(self.elf.unpack("<hhhh", 0x45cca0 + 3741 * 8), (128, 348, 542, 15))
        # AppNoConn background is outside any clip group.
        self.assertIsNone(raw[0x04][2][0].clip)
        clipped = [e for v in raw.values() for e in v[2] if e.clip is not None]
        self.assertEqual(len(clipped), 2394)
        self.assertEqual(len({e.clip for e in clipped}), 13)
        # The C++ tables carry the raw clip_table; the walk intersects at runtime.
        graph = self.gen.load_scene_graph(self.elf)
        self.assertEqual(graph["clips"][3741], (128, 348, 542, 15))
        source = self.gen.emit_cpp(self.elf)
        self.assertIn("const IgdtClipRect kClips[%d] = {" % len(graph["clips"]), source)
        self.assertIn("kTextures, 32110, kClips, %d," % len(graph["clips"]), source)

    def test_kind7_rects_present(self):
        self.require_firmware()
        screens = self.gen.load_screens(self.elf)
        rects = {sid: n for sid, n in
                 ((sid, sum(1 for e in s["elements"] if e["type"] == "rect"))
                  for sid, s in screens.items()) if n}
        self.assertEqual(rects, RECT_ELEMENTS)
        self.assertIn({"type": "rect", "tag": "0x07100000", "x": 0, "y": 0, "w": 800,
                       "h": 480, "alpha": 255, "color": "0x00000000"},
                      screens[0xdf]["elements"])

    def test_scene_graph_and_touch_paths(self):
        self.require_firmware()
        graph = self.gen.load_scene_graph(self.elf)
        # Every node reachable from the 255 screens' root lists, in stock
        # .rodata order (one dense block), child slices kept contiguous.
        nodes = graph["nodes"]
        self.assertEqual(len(nodes), 76513)
        for eidx, ccnt, tag, first in nodes:
            if ccnt:
                self.assertTrue(0 <= first and first + ccnt <= len(nodes))
            else:
                self.assertEqual(first, -1)
        self.assertEqual(len(graph["touch_paths"]), 2496)
        screens = graph["screens"]
        self.assertEqual(screens[0x04]["touch_count"], 1)
        self.assertEqual(screens[0x03]["touch_count"], 1)
        self.assertEqual(screens[0xf9]["touch_count"], 74)
        self.assertEqual((screens[0x04]["layer_group"], screens[0xfe]["layer_group"]), (1, 5))
        # A touch path is a root index then child steps, ending with the first
        # step >= the node's child count (FUN_00324b90).
        paths = graph["touch_paths"]
        for screen in screens.values():
            for first, count in paths[screen["touch_first"]:screen["touch_first"] + screen["touch_count"]]:
                steps = graph["touch_steps"][first:first + count]
                self.assertTrue(2 <= count <= 32)
                self.assertLess(steps[0], screen["root_count"])
                node = nodes[screen["root_first"] + steps[0]]
                for step in steps[1:-1]:
                    self.assertLess(step, node[1])
                    node = nodes[node[3] + step]
                self.assertGreaterEqual(steps[-1], node[1])
        self.assertEqual(len(graph["dyn_grps"]), 0x30ba)
        self.assertEqual(len(graph["dyn_imgs"]), 0x6cfd)
        self.assertEqual(len(graph["dyn_txts"]), 0xe2a)
        self.assertEqual(len(graph["branch_init"]), 0x10f0)
        self.assertEqual(len(graph["state_init"]), 0x1b11)
        self.assertEqual(graph["branch_init"][0x45], 1)
        source = self.gen.emit_cpp(self.elf)
        self.assertIn("const IgdtNode kNodes[76513] = {", source)
        self.assertIn("const IgdtTouchPath kTouchPaths[2496] = {", source)

    def test_screen_count_255(self):
        self.require_firmware()
        screens = self.gen.load_screens(self.elf)
        self.assertEqual(sorted(screens), list(range(255)))
        self.assertEqual(screens[0xfe]["screen_id"], "0xfe")

    def test_texture_dedup_2806(self):
        self.require_firmware()
        textures = self.gen.load_textures(self.elf)
        self.assertEqual(len(textures), 32110)
        paths = {self.elf.cstr(t[9]) for t in textures}
        self.assertEqual(len(paths), 2806)
        missing = sorted(p for p in paths if not (STOCK_OPT / p).is_file())
        self.assertEqual(missing, [])

    def test_missing_firmware_fails_fast(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "igdt_tables_vw.cpp"
            res = run_gen("--hmi", "/nonexistent", "--out", str(out))
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("/nonexistent", res.stderr)
            self.assertIn("tools/build_rootfs.sh", res.stderr)
            self.assertFalse(out.exists())

    def test_hmi_sha256_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            wrong = pathlib.Path(tmp) / "hmi"
            wrong.write_bytes(b"\x7fELF not the stock hmi")
            self.assertNotEqual(hashlib.sha256(wrong.read_bytes()).hexdigest(), STOCK_SHA256)
            out = pathlib.Path(tmp) / "igdt_tables_vw.cpp"
            res = run_gen("--hmi", str(wrong), "--out", str(out))
            self.assertNotEqual(res.returncode, 0)
            self.assertIn(STOCK_SHA256, res.stderr)
            self.assertIn(str(wrong), res.stderr)
            self.assertFalse(out.exists())

    def test_string_block_matches_json(self):
        self.require_firmware()
        import gen_strings
        oracle = json.loads(STRINGS_JSON.read_text())
        oracle.pop("hmi_path")      # absolute path of the machine that ran the old extractor
        decoded = gen_strings.decode_block(self.elf)
        self.assertEqual(decoded["neutral_count"], 222)
        self.assertEqual(decoded["multilang_count"], 1976)
        self.assertEqual(decoded, oracle)

    def test_block_location(self):
        self.require_firmware()
        import gen_strings
        self.assertEqual(gen_strings.BLOCK_FILE_OFFSET, 0x3c3680)
        self.assertEqual(self.elf.offset(gen_strings.BLOCK_VA), 0x3c3680)
        self.assertEqual(gen_strings.MAX_ID, 0x20fcb)
        self.assertEqual(gen_strings.MULTILANG_START, 0x02d2)
        decoded = gen_strings.decode_block(self.elf)
        self.assertEqual(decoded["multilang_start_offset"], "0x2d2")
        self.assertEqual(max(int(k, 16) for k in decoded["multilang"]), 0x20fcb)
        # The last 5-language group starts at the id limit and ends at 0x20fec.
        self.assertEqual(decoded["block_length"], "0x20fec")
        self.assertEqual(decoded["languages"], ["en_US", "zh_CN", "pt_BR", "ru_RU", "es_MX"])

    def test_text_elements_link_strings(self):
        self.require_firmware()
        import gen_strings
        raw = self.gen.load_screen_elements(self.elf)
        engineering = {int(e["offset_wchars"], 16): e["text"]
                       for e in gen_strings.decode_engineering(self.elf)}
        kinds = collections.Counter()
        for _, _, elements in raw.values():
            for e in elements:
                if e.kind != 4:
                    continue
                if (e.tag >> 24) & 0xf == 4:
                    # Kind 4 reads text_layout[eidx].+0 as a wchar offset into the
                    # engineering table (FUN_0031f41c).
                    self.assertIn(e.str_id, engineering)
                    kinds["engineering"] += 1
                else:
                    # Kind 5 text lives in a runtime buffer filled by page code.
                    self.assertEqual(e.str_id, self.gen.STR_ID_RUNTIME)
                    kinds["runtime"] += 1
        self.assertEqual(kinds, {"engineering": 74, "runtime": 2821})
        source = self.gen.emit_cpp(self.elf)
        self.assertIn("0x%08xu" % self.gen.STR_ID_RUNTIME, source)

    def test_kind5_runtime_offset_kept(self):
        self.require_firmware()
        raw = self.gen.load_screen_elements(self.elf)
        # AppNoConn-adjacent screen 0x02: first kind-5 text, text_layout[6] +0 = 774.
        first = next(e for e in raw[0x02][2] if e.kind == 4)
        self.assertEqual((first.tag, first.str_id, first.rt_off),
                         (0x05100000, self.gen.STR_ID_RUNTIME, 774))
        for _, _, elements in raw.values():
            for e in elements:
                if e.kind == 4 and (e.tag >> 24) & 0xf == 4:
                    self.assertEqual(e.rt_off, 0)
        self.assertIn("\n    {774u, 100, ", self.gen.emit_cpp(self.elf))

    def test_text_layouts_emitted(self):
        self.require_firmware()
        # FUN_003223d4 writes widget text into the runtime buffer at
        # text_layout[id] +0 (wchar offset), at most +6 wchars, for id < 0xe3f.
        layouts = self.gen.load_text_layouts(self.elf)
        self.assertEqual(len(layouts), 0xe3f)
        # vAppNoConnection widgets: VInfoTxt_L2 0x19, VInfoTxt 0x1a, VBtnInfo label 0x1c.
        self.assertEqual(layouts[0x19], (2805, 128))
        self.assertEqual(layouts[0x1a], (2934, 128))
        self.assertEqual(layouts[0x1c], (3192, 100))
        source = self.gen.emit_cpp(self.elf)
        # Text nodes use layouts up to 0xe8a; the store still accepts < 0xe3f.
        self.assertIn("const IgdtTextLayout kTextLayouts[3723] = {", source)
        self.assertIn("    {2934u, 128, 752, 8, 0x22, 0x02},", source)
        # Runtime buffer (0x00c62264) length: largest offset + max chars of a
        # runtime-text layout (+0x10 bit 1).
        self.assertIn("kTextLayouts, 3647, 410003u", source)

    def test_txt_bg_color(self):
        self.require_firmware()
        raw = self.gen.load_screen_elements(self.elf)
        # AppNoConn VInfoTxt (dynamic record 0x58): +0xc background 0xff000000.
        info = next(e for e in raw[0x04][2] if e.tag == 0x45100058)
        self.assertEqual(info.bg_color, 0xff000000)
        # kDynTxts[0x58]: alpha, layout 0x1a, x, y, fg, bg, matrix, pivot.
        self.assertIn("    {255, 26, 400, 113, 0xffffffffu, 0xff000000u, kId, 0.0f, 0.0f},",
                      self.gen.emit_cpp(self.elf))

    def test_decoder_rejects_corrupt_data(self):
        import gen_strings
        with self.assertRaises(gen_strings.StringError):      # surrogate U+D800
            gen_strings.decode_chunks(bytes([0x62, 0xd8, 0x00, 0x00, 0, 0, 0, 0]), 0)
        with self.assertRaises(gen_strings.StringError):      # runs past the block
            gen_strings.decode_chunks(bytes([0x80 | 1, 0x41]), 0)
        with self.assertRaises(gen_strings.StringError):
            gen_strings.string_end(bytes([0x81, 0x41]), 0)

    def test_strings_cpp_generated(self):
        self.require_firmware()
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "strings_vw.cpp"
            res = subprocess.run([sys.executable, str(TOOLS / "gen_strings.py"), "--hmi",
                                  str(STOCK_HMI), "--out", str(out)],
                                 capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(res.returncode, 0, res.stderr)
            source = out.read_text(encoding="utf-8")
            self.assertTrue(source.endswith("\n"))
            self.assertIn("{0x030fu, {\"Driver window\", ", source)
            self.assertIn("g_igdtStringsVw", source)
            self.assertEqual(source.count("\n    {0x"), 222 + 1976 + 76)

    def test_strings_missing_firmware_fails_fast(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "strings_vw.cpp"
            res = subprocess.run([sys.executable, str(TOOLS / "gen_strings.py"), "--hmi",
                                  "/nonexistent", "--out", str(out)],
                                 capture_output=True, text=True, cwd=ROOT)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("tools/build_rootfs.sh", res.stderr)
            self.assertFalse(out.exists())

    def test_shared_c_string_and_write_if_changed(self):
        import hmi_elf
        self.assertEqual(hmi_elf.c_string('a\\"??\n\u00e9'), '"a\\\\\\"\\077\\077\\012\\303\\251"')
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "sub" / "gen.cpp"
            self.assertTrue(hmi_elf.write_if_changed(out, "hello\n"))
            os.utime(out, (1000, 1000))
            self.assertFalse(hmi_elf.write_if_changed(out, "hello\n"))
            self.assertEqual(int(out.stat().st_mtime), 1000)
            self.assertTrue(hmi_elf.write_if_changed(out, "world\n"))
            self.assertEqual(out.read_text(encoding="utf-8"), "world\n")


if __name__ == "__main__":
    unittest.main()
