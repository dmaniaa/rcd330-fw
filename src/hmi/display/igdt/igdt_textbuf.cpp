// File name is ours; stock name unknown.
#include "display/igdt/igdt_textbuf.h"

#include <vector>

namespace {

const IgdtWchar kReplacement = 0xfffd;

// SetTextFromId (0x00320ec8) decodes the string into a 0x40-wchar buffer.
const size_t kTextIdMaxChars = 0x40;

// Stock keeps the buffer in .bss (0x00c62264, 410003 wchars for 5624). Ours
// is allocated for the tables it serves; switching tables starts a new,
// empty buffer (only host tests do that). One extra zero wchar past
// text_buffer_len terminates a last slot filled to max_chars (wcsncpy leaves
// it unterminated); readers of IgdtRuntimeText stop there.
std::vector<IgdtWchar> g_runtimeText;
const IgdtSkinTables* g_runtimeTextOwner = 0;

std::vector<IgdtWchar>& RuntimeBuffer(const IgdtSkinTables& t) {
    if (g_runtimeTextOwner != &t) {
        g_runtimeText.assign((size_t)t.text_buffer_len + 1, 0);
        g_runtimeTextOwner = &t;
    }
    return g_runtimeText;
}

// Length of a valid UTF-8 sequence starting at s and its code point, or 0
// when s[0] does not start one.
size_t DecodeUtf8(const unsigned char* s, IgdtWchar* cp) {
    const unsigned char c = s[0];
    size_t len;
    IgdtWchar min;
    if (c < 0x80) {
        *cp = c;
        return 1;
    } else if (c >= 0xc2 && c <= 0xdf) {
        len = 2;
        min = 0x80;
        *cp = c & 0x1f;
    } else if (c >= 0xe0 && c <= 0xef) {
        len = 3;
        min = 0x800;
        *cp = c & 0x0f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        len = 4;
        min = 0x10000;
        *cp = c & 0x07;
    } else {
        return 0;
    }
    for (size_t i = 1; i < len; ++i) {
        if ((s[i] & 0xc0) != 0x80) return 0;   // also stops at the terminator
        *cp = (*cp << 6) | (s[i] & 0x3f);
    }
    if (*cp < min || *cp > 0x10ffff || (*cp >= 0xd800 && *cp <= 0xdfff)) return 0;
    return len;
}

// 0x00325e24 IntersectRect: clips (*x, *y, *w, *h) to (x0, y0, w0, h0);
// false when they do not touch.
bool IntersectRect(int x0, int y0, int w0, int h0, int* x, int* y, int* w, int* h) {
    const int x1 = x0 + w0;
    const int rx1 = *x + *w;
    if (*x > x1 || rx1 < x0) return false;
    const int y1 = y0 + h0;
    if (*y > y1 || *y + *h < y0) return false;
    const int nx = *x < x0 ? x0 : *x;
    *w = (x1 < rx1 ? x1 : rx1) - nx;
    *x = nx;
    const int ry1 = *y + *h;
    const int ny = *y < y0 ? y0 : *y;
    *h = (y1 < ry1 ? y1 : ry1) - ny;
    *y = ny;
    return true;
}

} // namespace

size_t IgdtUtf8ToUtf32(const char* utf8, IgdtWchar* out, size_t out_len) {
    if (out_len == 0) return 0;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8 ? utf8 : "");
    size_t n = 0;
    while (*s && n + 1 < out_len) {
        IgdtWchar cp = 0;
        const size_t len = DecodeUtf8(s, &cp);
        if (len == 0) {
            out[n++] = kReplacement;
            ++s;
        } else {
            out[n++] = cp;
            s += len;
        }
    }
    out[n] = 0;
    return n;
}

// 0x003223d4 IgdtSetRuntimeText
int IgdtSetRuntimeText(const IgdtSkinTables& t, unsigned id, const IgdtWchar* text) {
    if (id >= t.text_layout_count || !text) return IGDT_ERR_PARAM;
    const IgdtTextLayout& layout = t.text_layouts[id];
    std::vector<IgdtWchar>& buf = RuntimeBuffer(t);
    if ((uint64_t)layout.off + layout.max_chars > t.text_buffer_len) {
        // DEVIATION: stock writes wherever text_layout points; ids whose
        // layout is not a runtime-buffer slot are refused here.
        return IGDT_ERR_PARAM;
    }
    // TODO(phase3): stock skips the copy when wcsncmp finds the text unchanged
    // and otherwise marks the id dirty (bit id & 7 of 0x00a11050[(id & 0x7ff) >> 3]).
    IgdtWchar* dst = &buf[layout.off];
    size_t i = 0;
    for (; i < layout.max_chars && text[i] != 0; ++i) dst[i] = text[i];
    for (; i < layout.max_chars; ++i) dst[i] = 0;
    return IGDT_OK;
}

// 0x00320ec8 IgdtSetRuntimeTextId
int IgdtSetRuntimeTextId(const IgdtSkinTables& t, const IgdtStringTables& strings, unsigned id,
                         uint32_t str_id, int lang) {
    // Stock decodes the compressed block straight to wchar (0x0031d1bc); our
    // tables hold UTF-8.
    IgdtWchar text[kTextIdMaxChars];
    IgdtUtf8ToUtf32(IgdtLookupString(strings, str_id, lang), text, kTextIdMaxChars);
    // TODO(phase3): DAT_00a0fa58 = 1 (redraw request) on success.
    return IgdtSetRuntimeText(t, id, text);
}

const IgdtWchar* IgdtRuntimeText(const IgdtSkinTables& t, uint32_t off) {
    std::vector<IgdtWchar>& buf = RuntimeBuffer(t);
    if (off >= t.text_buffer_len) return 0;
    return &buf[off];
}

// 0x00325ed8 IgdtBlitCoverage
void IgdtBlitCoverage(int x, int y, uint8_t* dst, int dst_w, int dst_h, const uint8_t* src, int src_w,
                      int src_rows) {
    if (!dst || dst_w == 0 || dst_h == 0 || !src || src_w == 0 || src_rows == 0) return;
    int cx = x, cy = y, cw = src_w, ch = src_rows;
    if (!IntersectRect(0, 0, dst_w, dst_h, &cx, &cy, &cw, &ch)) return;
    int sx = 0, sy = 0;
    if (x == 0 && y == 0) {
        if (cx == 0 && cy == 0 && dst_w == src_w && dst_h == src_rows) {
            for (int i = 0; i < dst_h * src_w; ++i) {
                if (dst[i] < src[i]) dst[i] = src[i];
            }
            return;
        }
    } else {
        if (x < 0) {
            sx = -x;
            x = 0;
        }
        if (y < 0) {
            sy = -y;
            y = 0;
        }
    }
    uint8_t* d = dst + dst_w * y + x;
    const uint8_t* s = src + src_w * sy + sx;
    for (int row = 0; row < ch; ++row) {
        for (int col = 0; col < cw; ++col) {
            if (d[col] < s[col]) d[col] = s[col];
        }
        d += dst_w;
        s += src_w;
    }
}

// 0x003278e0 IgdtTextAnchorX (placement part)
IgdtTextAnchor IgdtTextAnchorX(unsigned state_align, unsigned layout_align, uint16_t box_w, uint32_t pad,
                               uint32_t text_w) {
    const uint32_t box = box_w;
    if (text_w > box) text_w = box;
    const uint32_t span = pad + text_w;
    uint32_t shift;     // uVar19
    uint32_t origin;    // uVar28 -> sVar27
    if ((state_align & 1) == 0) {
        if ((state_align & 2) == 0) {
            shift = 0;
            origin = 0;
        } else {
            // STOCK-QUIRK: unsigned; wraps when pad makes span exceed the box.
            shift = (box - span) >> 1;
            origin = span >> 1;
        }
    } else {
        shift = box - span;
        origin = span - 1;
    }
    int16_t o = (int16_t)origin;
    const unsigned h = state_align & 3;
    const unsigned l = layout_align & 3;
    if (h == 2) {
        if (l == 0) {
            o = (int16_t)(o - (int16_t)(box >> 1));
        } else if (l == 1) {
            o = (int16_t)(o + (int16_t)box);
        }
    } else if (h == 1) {
        if (l == 0) {
            o = (int16_t)(o - (int16_t)box);
        } else if (l == 2) {
            o = (int16_t)(o - (int16_t)(box >> 1));
        }
    } else if (l == 2) {
        o = (int16_t)(o + (int16_t)(box >> 1));
    } else if (l == 1) {
        o = (int16_t)(o + (int16_t)box);
    }
    IgdtTextAnchor a;
    a.x_shift = (int16_t)(o + (int16_t)shift);
    a.pivot_shift = (int32_t)shift;
    return a;
}
