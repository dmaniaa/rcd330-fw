#!/usr/bin/env python3
"""Pixel parity of the recreated hmi against stock in QEMU, without masks.

Stock and the recreated hmi boot the same way and are driven by the same
real input: once the hmi is ready (Rcd330Qemu.wait_hmi_ready: the boot page,
tuner eScrId 0xf9, is up and pwr_agent's wake-state cycle is done), the
VIP wall clock set to 2026-11-24 12:00:00, hardkey APP -> AppNoConn
(eScrId 0x04), then a QMP touch on VBtnInfo -> AppConnectInfo (eScrId 0x03).
Each frame is grabbed with the QEMU monitor screendump once the display is
stable, and the entire 800x480 frame is compared: at least 99.5 % of all
pixels must be within +-4 per channel.

Language. The default is index 1, zh_CN: VDisplayProxy::Init (0x000531a8)
sets language 1 and the IGDT init (0x00060e3c) calls 0x0031d210(1). The
removed HMI_DEV_SCREEN hook rendered en_US, which caused the old mismatch.
STOCK-QUIRK: the language is decided once, early, by a race. When the PSS
HMI_SYSTEM_SETUP_PSS_NEWBLK (0x4b) read fails (always under QEMU),
VPSSProxy::RecieveVmfMsg (0x001c5a90) sets the region from
VAppConnectProxy::GetRegionMode (0x00347498): 1 (constructor default, so
zh_CN) unless the App-Connect PSS coding that diag writes during boot has
already been decoded, then 0 or 0xff, which gives en_US (0x00050abc ->
0x000509b0 -> 0x000508f0 -> 0x0006125c). The recreated hmi ports that
decision, so it inherits the race. Nothing logs the outcome, so
appnoconn_language() reads it from the pixels (ink width of the second
welcome line). Ours must be a known language (else fail). If stock landed
on the other one, the stock reference is re-booted up to STOCK_ATTEMPTS
times until it matches; if it never does, the tests are skipped with the
race and the languages seen. No text is excluded.

Negative controls keep the check from becoming vacuous: an all-black frame
and our frame with the screen's text line boxes blacked out must both score
below MIN_MATCH. (AppConnectInfo is about 90 % black, so the all-black
control is held to the pass threshold itself: a frame that draws nothing
must fail.)

On failure stock_*.ppm, ours_*.ppm, diff_<screen>.png and both serial logs
are left in build/hmi/parity/.

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
BOOT_TIMEOUT_S = 240
FRAME_TIMEOUT_S = 120
STEP_TIMEOUT_S = 30
SETTLE_S = 3
CLOCK = "TIME 2026 11 24 12 00 00"
INFO_XY = (400, 438)  # VBtnInfo: touch tag 0x40100167 at (0, 0x18c) 800x0x54

SCREEN_APPNOCONN = 0x04
SCREEN_APPCONNECTINFO = 0x03
SCREENS = {SCREEN_APPNOCONN: "appnoconn", SCREEN_APPCONNECTINFO: "appconnectinfo"}
TOLERANCE = 4
MIN_MATCH = 0.995
KNOWN_LANGUAGES = ("en_US", "zh_CN")
STOCK_ATTEMPTS = 3
# Language probe on AppNoConn: the centred second welcome line
# ("Please connect a device using the USB." / zh_CN equivalent). Ink is any
# pixel whose darkest channel is > LANG_PROBE_INK. Measured: en_US spans
# x 143..657 (514 px), zh_CN x 210..591 (381 px).
LANG_PROBE_ROWS = (165, 200)
LANG_PROBE_INK = 160
LANG_EN_MIN_WIDTH = 470
LANG_ZH_MAX_WIDTH = 430
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


def text_boxes(screen_id):
    import gen_igdt_tables
    import hmi_elf
    walker = gen_igdt_tables.SceneWalker(hmi_elf.Elf(STOCK_HMI))
    _, _, elements = walker.screen(screen_id)
    return [text_box(e) for e in elements if e.kind == gen_igdt_tables.ELEM_TXT]


def build_mask(boxes):
    """Selection of pixels (1 = selected) covered by boxes (x0, y0, x1, y1)."""
    mask = bytearray(W * H)
    for x0, y0, x1, y1 in boxes:
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, W), min(y1, H)
        for y in range(y0, y1):
            mask[y * W + x0:y * W + x1] = b"\1" * max(x1 - x0, 0)
    return mask


def compare(stock, ours, mask):
    """(matching, compared, diff_rgb) over the pixels mask leaves out (all
    of them for build_mask([])): diff is red where pixels differ, blue where
    excluded, dimmed stock elsewhere."""
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


def appnoconn_language(frame):
    """Classifies an AppNoConn frame as "en_US", "zh_CN" or "unknown".

    Stock writes no log line for its language switch (0x000508f0 and
    0x0006125c log nothing, and the setup-status write 0x001c3b18 that
    follows logs other fields), so the decision is read from the pixels:
    the ink width of the centred second welcome line.
    """
    xs = [x
          for y in range(*LANG_PROBE_ROWS)
          for x in range(W)
          if min(frame[(y * W + x) * 3:(y * W + x) * 3 + 3]) > LANG_PROBE_INK]
    if not xs:
        return "unknown"
    width = max(xs) - min(xs)
    if width >= LANG_EN_MIN_WIDTH:
        return "en_US"
    if width <= LANG_ZH_MAX_WIDTH:
        return "zh_CN"
    return "unknown"


def capture(qemu, prefix):
    """Drives one boot to 0x04 and 0x03; writes <prefix>_<screen>.ppm."""
    qemu.wait_hmi_ready(BOOT_TIMEOUT_S, FRAME_TIMEOUT_S)
    time.sleep(SETTLE_S)
    qemu.vip(CLOCK)
    since = qemu.mark()
    qemu.vip("BTN APP 1")
    time.sleep(0.3)
    qemu.vip("BTN APP 0")
    qemu.wait_serial(r"VPageManager::ShowPage 4\b", STEP_TIMEOUT_S, since=since)
    time.sleep(SETTLE_S)
    qemu.screendump_stable(OUT_DIR / f"{prefix}_{SCREENS[SCREEN_APPNOCONN]}.ppm")
    since = qemu.mark()
    qemu.touch(*INFO_XY)
    qemu.wait_serial(r"VPageManager::ShowPage 3\b", STEP_TIMEOUT_S, since=since)
    time.sleep(SETTLE_S)
    qemu.screendump_stable(OUT_DIR / f"{prefix}_{SCREENS[SCREEN_APPCONNECTINFO]}.ppm")


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
        cls.skip_reason = None
        cls.our_language_error = None
        with Rcd330Qemu(hmi=str(HMI)) as qemu:
            try:
                capture(qemu, "ours")
            finally:
                cls.serial = qemu.serial_text()
                (OUT_DIR / "ours_serial.txt").write_text(cls.serial)
        _, _, ours_noconn = read_ppm(OUT_DIR / f"ours_{SCREENS[SCREEN_APPNOCONN]}.ppm")
        our_language = appnoconn_language(ours_noconn)
        print(f"\nours: language {our_language}")
        if our_language not in KNOWN_LANGUAGES:
            cls.our_language_error = (
                f"the recreated hmi rendered AppNoConn in no known language "
                f"({our_language}; probe expects one of {KNOWN_LANGUAGES})")
            cls.failed = True
            return

        # STOCK-QUIRK: the language is decided in a boot race (VPSSProxy
        # 0x001c5a90 NEWBLK read failure -> region from the App-Connect coding
        # 0x00347498 -> 0x00050abc / 0x000509b0 / 0x000508f0). Ours ports the
        # decision and so inherits the race; each boot can land on either
        # language. Re-boot the stock reference until it matches ours.
        seen = []
        for attempt in range(1, STOCK_ATTEMPTS + 1):
            with Rcd330Qemu() as qemu:
                try:
                    capture(qemu, "stock")
                finally:
                    (OUT_DIR / "stock_serial.txt").write_text(qemu.serial_text())
            _, _, stock_noconn = read_ppm(OUT_DIR / f"stock_{SCREENS[SCREEN_APPNOCONN]}.ppm")
            language = appnoconn_language(stock_noconn)
            seen.append(language)
            print(f"stock boot {attempt}/{STOCK_ATTEMPTS}: language {language}")
            if language == our_language:
                break
        cls.stock_boots = len(seen)
        if seen[-1] != our_language:
            cls.skip_reason = (
                f"stock never rendered ours' {our_language} in {STOCK_ATTEMPTS} boots "
                f"(saw {', '.join(seen)}): stock's boot-time language race "
                f"(VPSSProxy 0x001c5a90 NEWBLK read failure + App-Connect "
                f"coding timing) picked another language; frames not comparable")
            cls.failed = True
            return
        cls.frames = {}
        for screen, name in SCREENS.items():
            sw, sh, stock = read_ppm(OUT_DIR / f"stock_{name}.ppm")
            ow, oh, ours = read_ppm(OUT_DIR / f"ours_{name}.ppm")
            cls.frames[screen] = ((sw, sh, ow, oh), stock, ours)
        cls.failed = False

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "failed", True):
            return
        for name in SCREENS.values():
            for side in ("stock", "ours"):
                (OUT_DIR / f"{side}_{name}.ppm").unlink(missing_ok=True)
        for name in ("stock_serial.txt", "ours_serial.txt"):
            (OUT_DIR / name).unlink(missing_ok=True)

    def check_full_frame(self, screen):
        if self.our_language_error:
            self.fail(self.our_language_error)
        if self.skip_reason:
            self.skipTest(self.skip_reason)
        name = SCREENS[screen]
        sizes, stock, ours = self.frames[screen]
        self.assertNotIn("Segmentation fault", self.serial)
        self.assertEqual(sizes, (W, H, W, H))
        everything = build_mask([])

        # Negative control 1: an all-black frame must fail badly.
        black_match, compared, _ = compare(stock, bytes(W * H * 3), everything)
        self.assertEqual(compared, W * H)
        black_ratio = black_match / compared
        print(f"\n{name} negative control (all-black): {black_match}/{compared} ({black_ratio:.4%})")
        self.assertLess(black_ratio, MIN_MATCH,
                        f"{name} all-black negative control matched {black_ratio:.4%} "
                        f">= {MIN_MATCH:.1%}: an empty frame would pass")

        matching, compared, diff = compare(stock, ours, everything)
        ratio = matching / compared
        print(f"{name} parity: {matching}/{compared} pixels within +-{TOLERANCE} ({ratio:.4%})")
        if ratio < MIN_MATCH:
            type(self).failed = True
            png = OUT_DIR / f"diff_{name}.png"
            write_png(png, W, H, bytes(diff))
            self.fail(f"{name} full-frame parity {ratio:.4%} < {MIN_MATCH:.1%}; "
                      f"see {OUT_DIR}/stock_{name}.ppm, ours_{name}.ppm, {png.name}")

        # Negative control 2: the text is part of the comparison.
        text = build_mask(text_boxes(screen))
        no_text = bytearray(ours)
        for i, inside in enumerate(text):
            if inside:
                no_text[i * 3:i * 3 + 3] = b"\x00\x00\x00"
        nt_match, nt_compared, _ = compare(stock, bytes(no_text), everything)
        nt_ratio = nt_match / nt_compared
        print(f"{name} negative control (text blacked out): {nt_match}/{nt_compared} ({nt_ratio:.4%})")
        self.assertLess(nt_ratio, MIN_MATCH,
                        f"{name} parity passed even with the text boxes blacked out ({nt_ratio:.4%})")

    def test_appnoconn_full_unmasked(self):
        self.check_full_frame(SCREEN_APPNOCONN)

    def test_appconnectinfo_full_unmasked(self):
        self.check_full_frame(SCREEN_APPCONNECTINFO)



def _bar_frame(x0, width):
    """A black AppNoConn-sized frame with one white bar in the probe band."""
    frame = bytearray(W * H * 3)
    for y in range(LANG_PROBE_ROWS[0] + 5, LANG_PROBE_ROWS[0] + 15):
        for x in range(x0, x0 + width):
            frame[(y * W + x) * 3:(y * W + x) * 3 + 3] = b"\xff\xff\xff"
    return bytes(frame)


class LanguageProbeTest(unittest.TestCase):
    """Fast: the AppNoConn language probe (no QEMU)."""

    def test_wide_second_line_is_en_us(self):
        self.assertEqual(appnoconn_language(_bar_frame(143, 515)), "en_US")

    def test_narrow_second_line_is_zh_cn(self):
        self.assertEqual(appnoconn_language(_bar_frame(210, 382)), "zh_CN")

    def test_between_thresholds_is_unknown(self):
        self.assertEqual(appnoconn_language(_bar_frame(170, 450)), "unknown")

    def test_empty_band_is_unknown(self):
        self.assertEqual(appnoconn_language(bytes(W * H * 3)), "unknown")


if __name__ == "__main__":
    unittest.main()
