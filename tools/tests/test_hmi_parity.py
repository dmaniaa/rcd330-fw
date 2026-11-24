#!/usr/bin/env python3
"""Pixel parity of the recreated hmi against stock in QEMU.

Stock boots to the radio page, APP opens AppNoConn (eScrId 0x04); the
recreated hmi draws screen 0x04 once through the HMI_DEV_SCREEN dev hook,
which also fills the runtime text buffer with the strings stock's
vAppNoConnection page writes. Both frames are grabbed once (setUpClass) with
the QEMU monitor screendump and compared outside a mask; at least 99.5 % of
the unmasked pixels must be within +-4 per channel.

test_appnoconn_images masks the line boxes of all txt elements of screen
0x04, the boxes of its dynamic-record images (page code state, phase 2), the
Info button glow (see button_state_boxes) and the status line (clock,
temperature and status icons are runtime data from other screens/page code).
test_appnoconn_full compares the text too: it masks only the dynamic-record
image boxes, the status line and the Info button glow below the Info text
(widget-owned branch state, see button_state_boxes).

Note: the mask boxes are derived from the same gen_igdt_tables.SceneWalker
that emits our compiled tables, so a bug shared with the walker would hide
behind them. check_parity therefore verifies a negative control (an all-black
frame, plus in test_appnoconn_full a frame with the text boxes blacked out)
scores well below MIN_MATCH in the exact same compared region.

On failure stock.ppm, ours.ppm, diff_<test>.png and both serial logs are
left in build/hmi/parity/.

Stock is "ready" once the first frame is on screen: the radio page was shown
("VPageManager::ShowPage 249") and gl_fb_shim logged "eglSwapBuffers #1".
After APP, "VPageManager::ShowPage 4" and the next swap mark AppNoConn; the
3 s wait lets any follow-up redraw settle.

Slow (two QEMU boots): skipped unless RCD330_QEMU_TESTS=1.
"""
import os
import pathlib
import struct
import sys
import time
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "src/hmi/tools"))

from hmi_qemu import Rcd330Qemu  # noqa: E402

HMI = ROOT / "build/hmi/hmi"
STOCK_HMI = ROOT / "extracted-fw/5624_vw/opt/hmi"
OUT_DIR = ROOT / "build/hmi/parity"
BANNER = r"\[HMI_START\]HMI main start"
BOOT_TIMEOUT_S = 240
FRAME_TIMEOUT_S = 120
SETTLE_S = 3

SCREEN_APPNOCONN = 0x04
# x0, y0, x1, y1 (exclusive); the separator at y=40 is compared.
STATUS_LINE = (0, 0, 800, 40)
TOLERANCE = 4
MIN_MATCH = 0.995
MAX_NEGATIVE_MATCH = 0.85
W, H = 800, 480


def read_ppm(path):
    data = pathlib.Path(path).read_bytes()
    magic, w, h, maxval, pixels = data.split(maxsplit=4)
    if magic != b"P6" or int(maxval) != 255:
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = int(w), int(h)
    if len(pixels) < w * h * 3:
        raise ValueError(f"{path}: truncated ({len(pixels)} of {w * h * 3} pixel bytes)")
    return w, h, pixels[:w * h * 3]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    pathlib.Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


FONT_STYLES, FONT_STYLE_STRIDE = 0x3cb25c, 0x14   # 48 styles, +0xd line height


def font_line_height(font):
    """Line height (= text texture height) of font style font, from stock's
    style table (0x003cb25c +0xd)."""
    import hmi_elf
    if not 0 <= font < 48:
        raise ValueError(f"font style {font} outside the 48-entry table")
    return hmi_elf.Elf(STOCK_HMI).u8(FONT_STYLES + font * FONT_STYLE_STRIDE + 0xd)


def text_box(e):
    """Screen box (x0, y0, x1, y1) a txt element may draw into.

    0x003278e0 places the line in the layout width e.w anchored at the record
    x: centred when align bits 0-1 are 2, ending at x when 1, else starting
    at x; the texture is one line high (font line height) from the record y.
    (e.h is the layout's max wchar count, not a height.)"""
    h_align = e.align & 3
    x0 = e.x - e.w // 2 if h_align == 2 else e.x - e.w if h_align == 1 else e.x
    return (x0, e.y, x0 + e.w, e.y + font_line_height(e.font))


def screen_elements(screen_id):
    import gen_igdt_tables
    import hmi_elf
    walker = gen_igdt_tables.SceneWalker(hmi_elf.Elf(STOCK_HMI))
    _, _, elements = walker.screen(screen_id)
    return elements


def dynamic_image_boxes(elements):
    """DEVIATION (controller ruling b: dynamic-record image state set by page
    code, phase 2): images with a dynamic record (tag bit 30) draw from RAM
    state that page code owns (texture choice, visibility, runtime scaling of
    3/9-slice widgets: the CarPlay/CarLife logo, the scroll glow at (0,89),
    the control-bar line at y=386); the static tables hold only their initial
    values, so their boxes are masked until phase 2."""
    import gen_igdt_tables
    return [(e.x, e.y, e.x + e.w, e.y + e.h) for e in elements
            if e.kind == gen_igdt_tables.ELEM_IMG and e.tag & gen_igdt_tables.TAG_DYNAMIC]


def image_mask_boxes(screen_id):
    import gen_igdt_tables
    elements = screen_elements(screen_id)
    boxes = [text_box(e) for e in elements if e.kind == gen_igdt_tables.ELEM_TXT]
    return boxes + dynamic_image_boxes(elements) + button_state_boxes(elements) + [STATUS_LINE]


# VBtnInfo's icon glow (static img 0x02100017) sits in child 1 of branch
# 0x01000045, whose branch_init selects 1 (highlighted); the button widget
# switches the branch to child 0 (icon without glow) at runtime, so stock
# shows no glow.
INFO_GLOW_TAG = 0x02100017
INFO_TEXT_TAG = 0x4510005a


def button_state_boxes(elements):
    """DEVIATION (extends ruling b: widget-owned branch state, phase 2): the
    Info glow box, cut off below the Info text's line box so the text itself
    is still compared (the glow's top rows inside the line box stay
    compared and differ; they are within the 0.5 % budget)."""
    by_tag = {e.tag: e for e in elements}
    glow, text = by_tag[INFO_GLOW_TAG], by_tag[INFO_TEXT_TAG]
    top = max(glow.y, text.y + font_line_height(text.font))
    return [(glow.x, top, glow.x + glow.w, glow.y + glow.h)]


def full_mask_boxes(screen_id):
    elements = screen_elements(screen_id)
    return dynamic_image_boxes(elements) + button_state_boxes(elements) + [STATUS_LINE]


def build_mask(boxes):
    mask = bytearray(W * H)
    for x0, y0, x1, y1 in boxes:
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, W), min(y1, H)
        for y in range(y0, y1):
            mask[y * W + x0:y * W + x1] = b"\1" * max(x1 - x0, 0)
    return mask


def compare(stock, ours, mask):
    """(matching, compared, diff_rgb): diff is red where pixels differ, blue
    where masked, dimmed stock elsewhere."""
    diff = bytearray(W * H * 3)
    matching = compared = 0
    for i in range(W * H):
        p = i * 3
        if mask[i]:
            diff[p:p + 3] = b"\x00\x00\x60"
            continue
        compared += 1
        a, b = stock[p:p + 3], ours[p:p + 3]
        if all(abs(a[c] - b[c]) <= TOLERANCE for c in range(3)):
            matching += 1
            diff[p:p + 3] = bytes(v // 3 for v in a)
        else:
            diff[p:p + 3] = b"\xff\x00\x00"
    return matching, compared, diff


@unittest.skipUnless(
    os.environ.get("RCD330_QEMU_TESTS") == "1",
    "set RCD330_QEMU_TESTS=1 to run QEMU parity tests",
)
class HmiParityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not HMI.is_file():
            raise AssertionError(f"{HMI} missing; run tools/build_hmi.sh")
        if not STOCK_HMI.is_file():
            raise AssertionError(f"{STOCK_HMI} missing; run tools/build_rootfs.sh")
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        stock_ppm, ours_ppm = OUT_DIR / "stock.ppm", OUT_DIR / "ours.ppm"

        with Rcd330Qemu() as qemu:
            try:
                qemu.wait_serial(BANNER, BOOT_TIMEOUT_S)
                qemu.wait_serial(r"VPageManager::ShowPage 249[\s\S]*gl_fb_shim: eglSwapBuffers #1",
                                 FRAME_TIMEOUT_S)
                qemu.vip("BTN APP 1")
                time.sleep(0.3)
                qemu.vip("BTN APP 0")
                qemu.wait_serial(r"VPageManager::ShowPage 4\b[\s\S]*gl_fb_shim: eglSwapBuffers #2",
                                 FRAME_TIMEOUT_S)
                time.sleep(SETTLE_S)
                qemu.screendump_stable(stock_ppm)
            finally:
                (OUT_DIR / "stock_serial.txt").write_text(qemu.serial_text())

        with Rcd330Qemu(hmi=str(HMI), env={"HMI_DEV_SCREEN": "0x%02x" % SCREEN_APPNOCONN}) as qemu:
            try:
                qemu.wait_serial(BANNER, BOOT_TIMEOUT_S)
                qemu.wait_serial(r"dev screen 0x04 drawn", FRAME_TIMEOUT_S)
                time.sleep(SETTLE_S)
                qemu.screendump_stable(ours_ppm)
            finally:
                cls.serial = qemu.serial_text()
                (OUT_DIR / "ours_serial.txt").write_text(cls.serial)

        sw, sh, cls.stock = read_ppm(stock_ppm)
        ow, oh, cls.ours = read_ppm(ours_ppm)
        cls.sizes = (sw, sh, ow, oh)
        cls.failed = False

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "failed", True):
            return
        for name in ("stock.ppm", "ours.ppm", "stock_serial.txt", "ours_serial.txt"):
            (OUT_DIR / name).unlink(missing_ok=True)

    def check_parity(self, name, boxes):
        self.assertNotIn("Segmentation fault", self.serial)
        self.assertEqual(self.sizes, (W, H, W, H))
        mask = build_mask(boxes)
        # Negative control: an all-black frame must fail badly in the exact
        # same compared region (guards against a vacuous or over-wide mask).
        black_match, black_compared, _ = compare(self.stock, bytes(W * H * 3), mask)
        self.assertGreater(black_compared, 0, f"AppNoConn {name} mask hid every pixel")
        black_ratio = black_match / black_compared
        print(
            f"\nAppNoConn {name} negative control (all-black): "
            f"{black_match}/{black_compared} ({black_ratio:.4%})"
        )
        self.assertLess(
            black_ratio,
            MAX_NEGATIVE_MATCH,
            f"AppNoConn {name} all-black negative control matched {black_ratio:.4%} "
            f">= {MAX_NEGATIVE_MATCH:.1%} (compared region mostly black?)",
        )

        matching, compared, diff = compare(self.stock, self.ours, mask)
        ratio = matching / compared
        print(
            f"AppNoConn {name} parity: {matching}/{compared} "
            f"unmasked pixels within +-{TOLERANCE} ({ratio:.4%})"
        )
        if ratio < MIN_MATCH:
            type(self).failed = True
            png = OUT_DIR / f"diff_{name}.png"
            write_png(png, W, H, bytes(diff))
            self.fail(
                f"AppNoConn {name} parity {ratio:.4%} < {MIN_MATCH:.1%}; "
                f"see {OUT_DIR}/stock.ppm, ours.ppm, {png.name}"
            )

    def test_appnoconn_images(self):
        self.check_parity("images", image_mask_boxes(SCREEN_APPNOCONN))

    def test_appnoconn_full(self):
        self.check_parity("full", full_mask_boxes(SCREEN_APPNOCONN))
        # Negative control for text rendering: blacking out the text boxes in
        # our frame (leaving all static images intact) must drop below MIN_MATCH.
        import gen_igdt_tables
        elements = screen_elements(SCREEN_APPNOCONN)
        text_mask = build_mask([
            text_box(e) for e in elements if e.kind == gen_igdt_tables.ELEM_TXT
        ])
        no_text = bytearray(self.ours)
        for i, masked in enumerate(text_mask):
            if masked:
                no_text[i * 3:i * 3 + 3] = b"\x00\x00\x00"
        mask = build_mask(full_mask_boxes(SCREEN_APPNOCONN))
        nt_match, nt_compared, _ = compare(self.stock, bytes(no_text), mask)
        nt_ratio = nt_match / nt_compared
        print(
            f"AppNoConn full negative control (text blacked out): "
            f"{nt_match}/{nt_compared} ({nt_ratio:.4%})"
        )
        self.assertLess(
            nt_ratio,
            MIN_MATCH,
            f"AppNoConn full parity passed even with text boxes blacked out ({nt_ratio:.4%})",
        )


if __name__ == "__main__":
    unittest.main()
