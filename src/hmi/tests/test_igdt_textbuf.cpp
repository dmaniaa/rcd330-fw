// File name is ours; stock name unknown.
// Host tests for display/igdt/igdt_textbuf.cpp: UTF-8 decoding, the runtime
// text store (0x003223d4 / 0x00320ec8), the coverage blit (0x00325ed8) and
// the text anchor math of 0x003278e0.
#include "display/igdt/igdt_textbuf.h"

#include <string.h>

#include "tests/test_runner.h"

namespace {

bool SameText(const IgdtWchar* a, const IgdtWchar* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

void TestUtf8Valid() {
    IgdtWchar out[16];
    CHECK(IgdtUtf8ToUtf32("Info", out, 16) == 4);
    const IgdtWchar info[] = {'I', 'n', 'f', 'o', 0};
    CHECK(SameText(out, info, 5));

    // 2-, 3- and 4-byte forms: "Д", "欢", U+1F600.
    CHECK(IgdtUtf8ToUtf32("\xd0\x94\xe6\xac\xa2\xf0\x9f\x98\x80", out, 16) == 3);
    const IgdtWchar mixed[] = {0x414, 0x6b22, 0x1f600, 0};
    CHECK(SameText(out, mixed, 4));

    CHECK(IgdtUtf8ToUtf32(0, out, 16) == 0);
    CHECK(out[0] == 0);
    CHECK(IgdtUtf8ToUtf32("", out, 16) == 0);
}

void TestUtf8Invalid() {
    IgdtWchar out[16];
    // Stray continuation, overlong "/" (C0 AF), surrogate U+D800 (ED A0 80),
    // above U+10FFFF (F4 90 80 80): every invalid byte is one U+FFFD.
    CHECK(IgdtUtf8ToUtf32("a\x80" "b", out, 16) == 3);
    const IgdtWchar stray[] = {'a', 0xfffd, 'b', 0};
    CHECK(SameText(out, stray, 4));

    CHECK(IgdtUtf8ToUtf32("\xc0\xaf", out, 16) == 2);
    CHECK(out[0] == 0xfffd && out[1] == 0xfffd && out[2] == 0);

    CHECK(IgdtUtf8ToUtf32("\xed\xa0\x80", out, 16) == 3);
    CHECK(out[0] == 0xfffd && out[1] == 0xfffd && out[2] == 0xfffd);

    CHECK(IgdtUtf8ToUtf32("\xf4\x90\x80\x80", out, 16) == 4);
    CHECK(out[0] == 0xfffd && out[3] == 0xfffd && out[4] == 0);

    // Truncated 3-byte sequence before the terminator.
    CHECK(IgdtUtf8ToUtf32("x\xe6\xac", out, 16) == 3);
    const IgdtWchar cut[] = {'x', 0xfffd, 0xfffd, 0};
    CHECK(SameText(out, cut, 4));
}

void TestUtf8OutputLimit() {
    IgdtWchar out[4] = {9, 9, 9, 9};
    CHECK(IgdtUtf8ToUtf32("abcdef", out, 4) == 3);
    const IgdtWchar abc[] = {'a', 'b', 'c', 0};
    CHECK(SameText(out, abc, 4));
    IgdtWchar untouched = 7;
    CHECK(IgdtUtf8ToUtf32("abc", &untouched, 0) == 0);
    CHECK(untouched == 7);
}

// Skin tables with only the text-store fields set (every other field 0).
IgdtSkinTables MakeTables(const IgdtTextLayout* layouts, uint16_t count, uint32_t buffer_len) {
    IgdtSkinTables t;
    memset(&t, 0, sizeof(t));
    t.text_layouts = layouts;
    t.text_layout_count = count;
    t.text_buffer_len = buffer_len;
    return t;
}

// Layouts: id 0 -> off 0 / 4 wchars, id 1 -> off 4 / 3, id 2 -> off 100 (past the buffer).
const IgdtTextLayout kLayouts[] = {{0u, 4, 0, 0, 0, 0}, {4u, 3, 0, 0, 0, 0}, {100u, 2, 0, 0, 0, 0}};
const IgdtSkinTables kTables = MakeTables(kLayouts, 3, 8u);

void TestRuntimeText() {
    const IgdtWchar* buf = IgdtRuntimeText(kTables, 0);
    CHECK(buf != 0);
    CHECK(IgdtRuntimeText(kTables, 7) == buf + 7);
    CHECK(IgdtRuntimeText(kTables, 8) == 0);
    for (int i = 0; i < 8; ++i) CHECK(buf[i] == 0);

    // wcsncpy: "abcdef" fills all 4 wchars of id 0 without a terminator.
    const IgdtWchar abcdef[] = {'a', 'b', 'c', 'd', 'e', 'f', 0};
    CHECK(IgdtSetRuntimeText(kTables, 0, abcdef) == IGDT_OK);
    const IgdtWchar abcd[] = {'a', 'b', 'c', 'd', 0};
    CHECK(SameText(buf, abcd, 5));

    // Shorter text is zero-padded to max_chars; the neighbour stays.
    const IgdtWchar xyz[] = {'x', 'y', 'z', 0};
    CHECK(IgdtSetRuntimeText(kTables, 1, xyz) == IGDT_OK);
    const IgdtWchar x[] = {'x', 0};
    CHECK(IgdtSetRuntimeText(kTables, 1, x) == IGDT_OK);
    CHECK(buf[3] == 'd' && buf[4] == 'x' && buf[5] == 0 && buf[6] == 0);

    CHECK(IgdtSetRuntimeText(kTables, 3, x) == IGDT_ERR_PARAM);    // id >= layout count
    CHECK(IgdtSetRuntimeText(kTables, 2, x) == IGDT_ERR_PARAM);    // outside the buffer
    CHECK(IgdtSetRuntimeText(kTables, 0, 0) == IGDT_ERR_PARAM);
}

// One slot that ends exactly at the end of an 8-wchar buffer.
const IgdtTextLayout kEndLayouts[] = {{6u, 2, 0, 0, 0, 0}};
const IgdtSkinTables kEndTables = MakeTables(kEndLayouts, 1, 8u);

void TestRuntimeTextEndOfBuffer() {
    const IgdtWchar xyz[] = {'x', 'y', 'z', 0};
    CHECK(IgdtSetRuntimeText(kEndTables, 0, xyz) == IGDT_OK);
    const IgdtWchar* slot = IgdtRuntimeText(kEndTables, 6);
    CHECK(slot != 0);
    // Filled to max_chars without a terminator (wcsncpy); the spare wchar
    // past the buffer still ends the string.
    CHECK(slot[0] == 'x' && slot[1] == 'y' && slot[2] == 0);
    size_t n = 0;
    while (slot[n] != 0) ++n;
    CHECK(n == 2);
}

const IgdtStringEntry kStrings[] = {
    {0x2d2u, {"Info", "\xe4\xbf\xa1\xe6\x81\xaf", "", "", ""}},
};
const IgdtStringTables kStringTables = {kStrings, 1, 0, 0};

} // namespace

// igdt_strings.cpp's public wrappers reference the generated tables.
extern const IgdtStringTables g_igdtStringsVw;
const IgdtStringTables g_igdtStringsVw = kStringTables;

namespace {

void TestRuntimeTextId() {
    const IgdtWchar* buf = IgdtRuntimeText(kTables, 0);
    CHECK(IgdtSetRuntimeTextId(kTables, kStringTables, 0, 0x2d2u, IGDT_LANG_ZH_CN) == IGDT_OK);
    const IgdtWchar zh[] = {0x4fe1, 0x606f, 0, 0};
    CHECK(SameText(buf, zh, 4));
    // pt_BR is empty -> en_US.
    CHECK(IgdtSetRuntimeTextId(kTables, kStringTables, 0, 0x2d2u, IGDT_LANG_PT_BR) == IGDT_OK);
    const IgdtWchar info[] = {'I', 'n', 'f', 'o'};
    CHECK(SameText(buf, info, 4));
    // Unknown id -> "" (all zero).
    CHECK(IgdtSetRuntimeTextId(kTables, kStringTables, 0, 0x9999u, IGDT_LANG_EN_US) == IGDT_OK);
    CHECK(buf[0] == 0 && buf[3] == 0);
    CHECK(IgdtSetRuntimeTextId(kTables, kStringTables, 5, 0x2d2u, IGDT_LANG_EN_US) == IGDT_ERR_PARAM);
}

void TestBlit() {
    uint8_t dst[4 * 3];
    memset(dst, 0x10, sizeof(dst));
    const uint8_t src[2 * 2] = {0x05, 0x80, 0xff, 0x22};
    // Interior: max blend keeps the larger value.
    IgdtBlitCoverage(1, 1, dst, 4, 3, src, 2, 2);
    const uint8_t a[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x80, 0x10, 0x10, 0xff, 0x22, 0x10};
    CHECK(memcmp(dst, a, sizeof(a)) == 0);

    // Negative origin clips the source's first row and column.
    memset(dst, 0, sizeof(dst));
    IgdtBlitCoverage(-1, -1, dst, 4, 3, src, 2, 2);
    CHECK(dst[0] == 0x22 && dst[1] == 0 && dst[4] == 0);
    // Right/bottom edge: only the source's top-left pixel lands at (3, 2).
    memset(dst, 0, sizeof(dst));
    IgdtBlitCoverage(3, 2, dst, 4, 3, src, 2, 2);
    CHECK(dst[11] == 0x05 && dst[10] == 0);
    // Entirely outside, or any zero size: untouched.
    memset(dst, 0, sizeof(dst));
    IgdtBlitCoverage(5, 0, dst, 4, 3, src, 2, 2);
    IgdtBlitCoverage(0, 0, dst, 4, 3, src, 0, 2);
    IgdtBlitCoverage(0, 0, dst, 4, 3, 0, 2, 2);
    for (size_t i = 0; i < sizeof(dst); ++i) CHECK(dst[i] == 0);

    // Same size at the origin: whole-buffer max blend.
    uint8_t same[4] = {0x40, 0x40, 0x40, 0x40};
    IgdtBlitCoverage(0, 0, same, 2, 2, src, 2, 2);
    const uint8_t b[] = {0x40, 0x80, 0xff, 0x40};
    CHECK(memcmp(same, b, sizeof(b)) == 0);
}

void TestAnchor() {
    // Centre in a centred 752-wide box: starts text_w / 2 left of x.
    IgdtTextAnchor c = IgdtTextAnchorX(2, 2, 752, 0, 300);
    CHECK(c.x_shift == 376 && c.pivot_shift == 226);
    // Left / left: no shift.
    IgdtTextAnchor l = IgdtTextAnchorX(0, 0, 752, 0, 300);
    CHECK(l.x_shift == 0 && l.pivot_shift == 0);
    // Right / right: ends one pixel right of x (stock: text_w - 1).
    IgdtTextAnchor r = IgdtTextAnchorX(1, 1, 752, 0, 300);
    CHECK(r.x_shift == 751 && r.pivot_shift == 452);
    // Centred text in a left-anchored box: starts (box - text) / 2 right of x.
    IgdtTextAnchor cl = IgdtTextAnchorX(2, 0, 752, 0, 300);
    CHECK(cl.x_shift == 0 && cl.pivot_shift == 226);
    // Text wider than the box is clamped to the box.
    IgdtTextAnchor wide = IgdtTextAnchorX(2, 2, 100, 0, 300);
    CHECK(wide.x_shift == 50 && wide.pivot_shift == 0);
}

} // namespace

int main() {
    TestUtf8Valid();
    TestUtf8Invalid();
    TestUtf8OutputLimit();
    TestRuntimeText();
    TestRuntimeTextId();
    TestRuntimeTextEndOfBuffer();
    TestBlit();
    TestAnchor();
    return TEST_RESULT();
}
