// File name is ours; stock name unknown.
#include "display/igdt/igdt_text.h"

#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "display/igdt/igdt_gl.h"
#include "service/osal/osal_api.h"

namespace {

enum { kFontCount = 0x30, kCjkSlot = 0x18 };

// 0x003cb25c font style +0xc flags: bit 1 = no text, bit 2 = FreeType face
// (otherwise a prebuilt bitmap glyph table; none in 5624: all 48 styles have
// flags 0x04), bit 0 = not loadable as a face.
enum { kStyleNoText = 0x02, kStyleFreeType = 0x04, kStyleLoadMask = 0x05 };

struct FontStyle {
    uint8_t flags;      // +0xc
    uint8_t line_h;     // +0xd line height = texture height in pixels
    const char* path;   // +0x10, relative to the font root
    float points;       // FT_Set_Char_Size height in points at 96 dpi (0x0031d2a4)
    int8_t y_adjust;    // per-face-slot baseline tweak of 0x003278e0
};

const char kLight[] = "gresfiles/VWThesis MQB Light.ttf";
const char kRegular[] = "gresfiles/VWThesis MQB Regular.ttf";
const char kCjk[] = "gresfiles/FZHT_GB18030.ttf";

// 0x003cb25c (20 B per style: flags, line height, path) with the point sizes
// of the 0x0031d2a4 switch and the baseline tweaks of the 0x003278e0 switch
// (0x003282e4..0x0032915c). Styles 24..47 are the CJK fallbacks of 0..23.
const FontStyle kFontStyles[kFontCount] = {
    {0x04, 55, kLight, 31.0f, 3},   {0x04, 45, kRegular, 26.0f, 2}, {0x04, 48, kRegular, 25.0f, 4},
    {0x04, 27, kRegular, 16.0f, 0}, {0x04, 75, kRegular, 48.0f, -1}, {0x04, 60, kRegular, 31.5f, 4},
    {0x04, 37, kRegular, 21.0f, 1}, {0x04, 22, kRegular, 13.0f, 0}, {0x04, 49, kLight, 24.0f, 4},
    {0x04, 50, kLight, 28.0f, 1},   {0x04, 47, kRegular, 26.0f, 3}, {0x04, 49, kLight, 25.0f, 3},
    {0x04, 50, kLight, 26.0f, 2},   {0x04, 56, kLight, 30.5f, 2},   {0x04, 27, kRegular, 14.0f, 2},
    {0x04, 28, kLight, 20.0f, -2},  {0x04, 28, kLight, 20.0f, -2},  {0x04, 29, kRegular, 20.0f, -1},
    {0x04, 25, kRegular, 17.0f, -1}, {0x04, 29, kRegular, 21.0f, -2}, {0x04, 27, kRegular, 19.0f, -3},
    {0x04, 28, kRegular, 18.0f, 0}, {0x04, 21, kLight, 13.0f, 0},   {0x04, 49, kLight, 24.0f, 4},
    {0x04, 55, kCjk, 31.0f, 4},     {0x04, 45, kCjk, 26.0f, 3},     {0x04, 48, kCjk, 25.0f, 5},
    {0x04, 27, kCjk, 16.0f, 1},     {0x04, 75, kCjk, 48.0f, 0},     {0x04, 60, kCjk, 32.0f, 0},
    {0x04, 37, kCjk, 20.0f, 2},     {0x04, 22, kCjk, 12.0f, 0},     {0x04, 49, kCjk, 25.0f, 4},
    {0x04, 50, kCjk, 26.0f, 2},     {0x04, 47, kCjk, 26.0f, 2},     {0x04, 49, kCjk, 25.5f, 4},
    {0x04, 50, kCjk, 26.0f, 3},     {0x04, 56, kCjk, 29.0f, 1},     {0x04, 27, kCjk, 15.0f, 1},
    {0x04, 28, kCjk, 19.5f, 0},     {0x04, 28, kCjk, 19.5f, 0},     {0x04, 29, kCjk, 19.5f, 0},
    {0x04, 25, kCjk, 17.0f, 0},     {0x04, 29, kCjk, 20.0f, 0},     {0x04, 27, kCjk, 19.0f, 0},
    {0x04, 28, kCjk, 18.0f, 0},     {0x04, 21, kCjk, 13.0f, 0},     {0x04, 49, kCjk, 25.0f, 4},
};

// FT_Load_Glyph flags: FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT (0x24) to
// draw and measure, FT_LOAD_FORCE_AUTOHINT (0x20) for the ellipsis advance.
const FT_Int32 kLoadRender = FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT;
const FT_Int32 kLoadMetrics = FT_LOAD_FORCE_AUTOHINT;

// 0x003278e0 local_628 / 0x00329ea0 output: at most 0x140 wchars + 0.
const unsigned kLineMax = 0x140;

FT_Library g_library = 0;                 // 0x00a0f964
FT_Face g_faces[kFontCount];              // 0x00a0f968 (IGDT state +0x60)
bool g_faceTried[kFontCount];
std::string g_fontRoot;
bool g_monoLogged = false;

// 0x00329624 FontIndex
unsigned FontIndex(unsigned font) {
    return font < kFontCount ? font : 0;
}

// Face of style slot (NULL when not loaded); slot >= 48 has none.
FT_Face Face(unsigned slot) {
    return slot < kFontCount ? g_faces[slot] : 0;
}

// Loads style slot's face once and sets its size; true when it is loaded.
// DEVIATION: stock retries FT_New_Face on every call after a failure; ours
// logs the path once and gives up.
// 0x0031d2a4 LoadFace
bool LoadFace(unsigned slot) {
    if (slot >= kFontCount || !g_library) return false;
    if (g_faces[slot]) return true;
    if (g_faceTried[slot]) return false;
    g_faceTried[slot] = true;
    const FontStyle& style = kFontStyles[slot];
    if ((style.flags & kStyleLoadMask) != kStyleFreeType) return false;
    const std::string path = g_fontRoot.empty() ? std::string(style.path) : g_fontRoot + "/" + style.path;
    if (FT_New_Face(g_library, path.c_str(), 0, &g_faces[slot]) != 0) {
        g_faces[slot] = 0;
        log_simple("IGDT", "IGDT: cannot load font %s\n", path.c_str());
        return false;
    }
    FT_Set_Char_Size(g_faces[slot], 0, (FT_F26Dot6)((double)style.points * 64.0), 96, 96);
    return true;
}

// Style font and, for 0..23, its CJK fallback in slot font + 24.
// 0x0031d5ec LoadFaces
void LoadFaces(unsigned font) {
    LoadFace(font);
    if (font < kCjkSlot) LoadFace(font + kCjkSlot);
}

// Face for character c in style font: the style's face, else its fallback
// (slot font + 24) when the style face lacks c. *slot_out = the face slot.
// The glyph slot of the face that was tried last stays in *glyph, as stock
// keeps using it when no face loads.
FT_UInt CharIndex(unsigned font, FT_ULong c, FT_Face* face, FT_GlyphSlot* glyph, unsigned* slot_out) {
    *face = Face(font);
    if (*face) *glyph = (*face)->glyph;
    *slot_out = font;
    FT_UInt index = FT_Get_Char_Index(*face, c);
    if (index == 0) {
        *face = Face(font + kCjkSlot);
        if (*face) *glyph = (*face)->glyph;
        *slot_out = font + kCjkSlot;
        index = FT_Get_Char_Index(*face, c);
    }
    return index;
}

// 0x00329ea0 param_1 (auStack_8c in 0x003278e0).
struct LineLayout {
    const IgdtWchar* text;  // +0x04
    uint16_t max_w;         // +0x0c
    uint16_t width;         // +0x0e out: laid-out width
    // STOCK-QUIRK: +0x10 is a byte in 0x00329ea0 / 0x003278e0 (local_7c), so
    // counts of 256..320 wrap and the rasterizer draws fewer wchars (no
    // overread: it only shortens the line).
    uint8_t count;          // +0x10 out: wchars in out (escapes included)
    uint8_t ellipsis;       // +0x14 "..." allowed (0x003278e0 passes 1)
};

// Copies n wchars of text and terminates (0x00329ea0 caps n at 0x140).
void CopyLine(IgdtWchar* out, const IgdtWchar* text, unsigned n) {
    if (n > kLineMax) n = kLineMax;
    for (unsigned i = 0; i < n; ++i) out[i] = text[i];
    out[n] = 0;
}

// FreeType branch of 0x00329ea0: measures l.text in style font; when it is
// wider than l.max_w drops trailing wchars (and appends "..." when l.ellipsis
// and trunc_mode bit 1 allow) until it fits. Copies the result to out
// (kLineMax + 1 wchars) when out is not NULL. Returns 1 when truncated.
// 0x00329ea0 LayoutLine
int LayoutLine(LineLayout& l, unsigned font, unsigned trunc_mode, IgdtWchar* out) {
    l.count = 0;
    l.width = 0;
    if (font >= kFontCount || !l.text) return 0;
    const FontStyle& style = kFontStyles[font];
    if (style.flags & kStyleNoText) return 0;
    if (!(style.flags & kStyleFreeType)) {
        // Bitmap glyph tables (0x0032964c): no 5624 style uses them.
        return 0;
    }
    LoadFaces(font);
    if (!Face(font)) return 0;

    FT_GlyphSlot glyph = Face(font)->glyph;
    FT_Face face = 0;
    unsigned slot = 0;
    int w = 0;
    unsigned n = 0;
    int glyphs = 0;    // iVar20: glyphs measured
    const IgdtWchar* p = l.text;
    IgdtWchar c = *p;
    while (c != 0) {
        if (c == 1) {
            // 0x01 r g: colour escape (0x00325c84), counted but not measured.
            if (p[1] != 0 && p[2] != 0) {
                c = p[3];
                p += 3;
                n += 3;
                if (c == 0) break;
            } else {
                const unsigned skip = p[1] != 0 ? 2 : 1;
                p += skip;
                n += skip;
                break;
            }
        } else if (c == 2) {
            // 0x02 dx: horizontal gap of dx pixels.
            if (p[1] == 0) {
                p += 1;
                n += 1;
                break;
            }
            c = p[2];
            w += (int)p[1];
            p += 2;
            n += 2;
            if (c == 0) break;
        }
        const FT_UInt index = CharIndex(font, *p, &face, &glyph, &slot);
        FT_Load_Glyph(face, index, kLoadRender);
        if (glyphs == 0 && glyph->bitmap_left < 0) {
            w += p[1] == 0 ? glyph->bitmap.width : (int)(glyph->advance.x >> 6) - glyph->bitmap_left;
        } else if (p[1] == 0 && *p != ' ') {
            w += glyph->bitmap_left + glyph->bitmap.width;   // last glyph: ink extent
        } else {
            w += (int)(glyph->advance.x >> 6);
        }
        ++p;
        ++n;
        ++glyphs;
        c = *p;
    }
    l.width = (uint16_t)w;
    l.count = (uint8_t)n;
    if (w < 1) return 0;
    if (w <= (int)l.max_w) {
        if (out) {
            CopyLine(out, l.text, n);
            l.count = (uint8_t)(n > kLineMax ? kLineMax : n);
        }
        return 0;
    }

    int limit = (int16_t)l.max_w;
    int dots_w = 0;
    unsigned dots_n = 0;
    if (l.ellipsis && (trunc_mode & 2)) {
        face = Face(font);
        if (face) glyph = face->glyph;
        if (FT_Get_Char_Index(face, '.') == 0) {
            // STOCK-QUIRK: the fallback face is used even when it is NULL
            // (FT_Load_Char then fails and the last glyph's advance is used).
            face = Face(font + kCjkSlot);
            if (face) glyph = face->glyph;
        }
        FT_Load_Char(face, '.', kLoadMetrics);
        dots_w = (int)glyph->advance.x * 3;
        if (dots_w > 0) dots_w >>= 6;
        if (dots_w < (int)l.max_w) {
            limit -= dots_w;
            dots_n = 3;
        } else {
            dots_w = 0;
        }
    }
    if (n == 0) {
        l.count = 0;
        return 1;
    }
    --p;
    for (;;) {
        const FT_UInt index = CharIndex(font, *p, &face, &glyph, &slot);
        --n;
        FT_Load_Glyph(face, index, kLoadRender);
        if (n == 0 && glyph->bitmap_left < 0) {
            w += glyphs == 1 ? -glyph->bitmap.width : glyph->bitmap_left - (int)(glyph->advance.x >> 6);
        } else if ((unsigned)(glyphs - 1) == n) {
            w -= glyph->bitmap_left + glyph->bitmap.width;
        } else {
            w -= (int)(glyph->advance.x >> 6);
        }
        l.width = (uint16_t)w;
        if (w <= limit) {
            if (!out) {
                l.count = (uint8_t)n;
                return 1;
            }
            if (dots_w < 1) {
                CopyLine(out, l.text, n);
                l.count = (uint8_t)(n > kLineMax ? kLineMax : n);
                return 1;
            }
            if (n < kLineMax - 2) {
                CopyLine(out, l.text, n);
                for (unsigned i = n; i < n + dots_n; ++i) out[i] = '.';
                out[n + dots_n] = 0;
                l.width = (uint16_t)(l.width + dots_w);
                l.count = (uint8_t)(n + dots_n);
                return 1;
            }
        }
        --p;
        if (n == 0) break;
    }
    l.count = 0;
    return 1;
}

// Rasterizes the laid-out line (count wchars of line) of style font into the
// w x line_h coverage buffer, glyph by glyph as 0x003278e0 does. Colour
// escapes are appended to escapes as (pw[1], pw[2]) pairs.
// 0x003278e0 RasterizeLine (FreeType branch, inner loop)
void RasterizeLine(const IgdtWchar* line, unsigned count, unsigned font, unsigned line_h, uint8_t* dst,
                   unsigned w, std::vector<uint32_t>& escapes) {
    int pen = 0;               // iVar33
    unsigned drawn = 0;        // cVar18: glyphs blitted
    unsigned rem = count;      // uVar25
    const IgdtWchar* p = line;
    FT_GlyphSlot glyph = 0;
    while (rem != 0) {
        unsigned rem1 = rem - 1;
        const IgdtWchar* q = p;
        if (*p == 1) {
            if (rem1 >= 2) {
                escapes.push_back(p[1]);   // 0x00325c84 sets fgColor; skips 3
                escapes.push_back(p[2]);
                q = p + 3;
                rem1 = rem1 < 3 ? 0 : rem1 - 3;
            }
        } else if (*p == 2 && rem1 != 0) {
            q = p + 1;
            rem1 = rem - 2;
            if (p[1] != 0) {
                pen += (int)p[1];
                if (rem1 != 0) {
                    q = p + 2;
                    rem1 = rem - 3;
                }
            }
        }
        FT_Face face = 0;
        unsigned slot = 0;
        const FT_UInt index = CharIndex(font, *q, &face, &glyph, &slot);
        p = q + 1;
        rem = rem1;
        if (!face) {
            // DEVIATION: stock goes on with the previous glyph's bitmap.
            continue;
        }
        const int h = (int)(face->size->metrics.height >> 6);
        const int base = h + (int)(face->size->metrics.descender >> 6);
        FT_Load_Glyph(face, index, kLoadRender);
        if (!glyph->bitmap.buffer) {
            pen += (int)(glyph->advance.x >> 6);
            continue;
        }
        int y = base + ((int)line_h - h) / 2 - glyph->bitmap_top;
        if (slot < kFontCount) y += kFontStyles[slot].y_adjust;
        // STOCK-QUIRK: rows are read with stride bitmap.width, not pitch
        // (equal for FreeType 2.4.8 gray bitmaps).
        const FT_Bitmap& bm = glyph->bitmap;
        const bool blit = bm.pixel_mode == FT_PIXEL_MODE_GRAY && bm.pitch >= bm.width;
        if (!blit && !g_monoLogged) {
            // DEVIATION: stock would read a 1-bit/embedded bitmap as 8-bit.
            log_simple("IGDT", "IGDT: glyph U+%04x has a non-gray bitmap; skipped\n", (unsigned)*q);
            g_monoLogged = true;
        }
        int advance;
        if (drawn == 0 && glyph->bitmap_left < 0) {
            if (blit) IgdtBlitCoverage(pen, y, dst, (int)w, (int)line_h, bm.buffer, bm.width, bm.rows);
            advance = (int)(glyph->advance.x >> 6) - glyph->bitmap_left;
        } else {
            if (blit) {
                IgdtBlitCoverage(glyph->bitmap_left + pen, y, dst, (int)w, (int)line_h, bm.buffer, bm.width,
                                 bm.rows);
            }
            advance = (int)(glyph->advance.x >> 6);
        }
        ++drawn;
        pen += advance;
    }
}

} // namespace

bool IgdtTextInit(const char* font_root) {
    g_fontRoot = font_root ? font_root : "";
    if (g_library) return true;
    if (FT_Init_FreeType(&g_library) != 0) {
        g_library = 0;
        log_simple("IGDT", "IGDT: FT_Init_FreeType failed\n");
        return false;
    }
    return true;
}

void IgdtTextTerm() {
    for (unsigned i = 0; i < kFontCount; ++i) {
        if (g_faces[i]) FT_Done_Face(g_faces[i]);
        g_faces[i] = 0;
        g_faceTried[i] = false;
    }
    if (g_library) FT_Done_FreeType(g_library);
    g_library = 0;
}

// STOCK-QUIRK: two font sources, as stock: the text_layout font (e.font,
// layout +7) picks the style, the face preload and the line-cache key, while
// line layout (0x00329ea0) and rasterization read the draw state's font
// (s.font, state +0x40). DrawText sets s.font = e.font just before, so they
// only differ if page code restyles the state in between.
// 0x003278e0 IgdtDrawText
void IgdtDrawText(const IgdtElement& e, const IgdtWchar* text) {
    IgdtDrawState& s = IgdtCurrentState();
    const unsigned layout_font = FontIndex(e.font);
    LoadFaces(layout_font);
    if (s.flags != 0 || e.font >= kFontCount || !text) return;
    const FontStyle& style = kFontStyles[e.font];
    // TODO(phase2): glow (text_layout +0x10 bit 0 / +9 radius / +0xc sigma,
    // all RAM-only, zero until page code sets them: pad = 2 * radius and the
    // blur pass) and the caller's width override (0x0031f41c param_3).
    const uint32_t pad = 0;
    LineLayout l;
    l.text = text;
    l.max_w = (uint16_t)(e.w - (int)pad);
    l.width = 0;
    l.count = 0;
    l.ellipsis = 1;
    IgdtWchar line[kLineMax + 1];
    line[0] = 0;
    LayoutLine(l, s.font, (s.text_align >> 4) & 3, line);
    // DEVIATION: a negative layout width (only possible with 0x02 escapes)
    // would make stock rasterize an uninitialized line.
    if (l.width == 0 || (int16_t)l.width < 0) return;
    if (!(style.flags & kStyleFreeType)) return;   // bitmap glyph path unused in 5624

    const IgdtTextAnchor a = IgdtTextAnchorX(s.text_align, e.align, (uint16_t)e.w, pad, l.width);
    s.pivot_x -= (float)a.pivot_shift;
    s.x = (int16_t)(s.x - a.x_shift);

    // TODO(phase2): stock caches 100 line textures keyed by text and style
    // (0x00e5595c) and rasterizes only on a miss.
    std::vector<uint8_t> coverage((size_t)style.line_h * l.width, 0);
    std::vector<uint32_t> escapes;
    RasterizeLine(line, l.count, FontIndex(s.font), style.line_h, &coverage[0], l.width, escapes);

    IgdtTextCoverage c;
    c.alpha = &coverage[0];
    c.w = l.width;
    c.h = style.line_h;
    c.fill_w = pad + (l.width < e.w ? l.width : (uint32_t)e.w);
    c.fill_h = style.line_h + pad;
    c.fg_escapes = escapes.empty() ? 0 : &escapes[0];
    c.fg_escape_count = escapes.size() / 2;
    IgdtDrawTextCoverage(c);
}
