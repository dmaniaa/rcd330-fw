// File name is ours; stock name unknown.
//
// IGDT text helpers that need neither FreeType nor GL: the runtime text
// buffer page code writes widget text into (stock 0x00c62264, reached as
// *(IGDT state + 0x2e088)), UTF-8 -> UTF-32 conversion of our generated
// string tables, and the glyph coverage blit and horizontal anchoring of the
// stock text draw (0x003278e0). Host-testable.
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_TEXTBUF_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_TEXTBUF_H

#include <stddef.h>
#include <stdint.h>

#include "display/igdt/igdt_strings.h"
#include "display/igdt/igdt_tables.h"

// Stock wchar_t: 4 bytes, one UTF-32 code point.
typedef uint32_t IgdtWchar;

// Stock status codes returned by the text store.
enum { IGDT_OK = 0, IGDT_ERR_PARAM = 0x1002 };

// Decodes NUL-terminated UTF-8 into at most out_len - 1 code points plus a
// terminating 0 (out_len 0 writes nothing). Each invalid byte (stray
// continuation, overlong form, surrogate, > U+10FFFF, truncated sequence)
// becomes U+FFFD. Returns the number of code points written. utf8 NULL
// counts as "".
size_t IgdtUtf8ToUtf32(const char* utf8, IgdtWchar* out, size_t out_len);

// 0x003223d4: copies text into the runtime buffer at text_layouts[id].off,
// wcsncpy-style (stops after max_chars wchars, zero-pads after the text's
// end; no terminator when text is that long). IGDT_ERR_PARAM when id is not
// below text_layout_count (stock: 0xe3f) or text is NULL. The buffer
// (t.text_buffer_len wchars, zero-filled) is created on first use.
int IgdtSetRuntimeText(const IgdtSkinTables& t, unsigned id, const IgdtWchar* text);

// 0x00320ec8: the text of string str_id in language lang (en_US fallback,
// IgdtLookupString) as at most 0x40 - 1 wchars, stored with
// IgdtSetRuntimeText.
int IgdtSetRuntimeTextId(const IgdtSkinTables& t, const IgdtStringTables& strings, unsigned id,
                         uint32_t str_id, int lang);

// Runtime text at wchar offset off (*(state + 0x2e088) + off, read by kind-5
// text nodes in 0x0031f41c); NULL when off is outside the buffer. Always
// 0-terminated within the allocation (one spare wchar past the last slot).
const IgdtWchar* IgdtRuntimeText(const IgdtSkinTables& t, uint32_t off);

// 0x00325ed8: max-blends a src_w x src_rows 8-bit coverage bitmap (row
// stride src_w) into the dst_w x dst_h buffer dst with its top-left at
// (x, y), clipped to dst (0x00325e24). Does nothing when any size is 0.
void IgdtBlitCoverage(int x, int y, uint8_t* dst, int dst_w, int dst_h, const uint8_t* src, int src_w,
                      int src_rows);

// Horizontal placement of a text texture (0x003278e0). state_align = draw
// state +0x3f bits 0-1, layout_align = text_layout +8 bits 0-1, box_w =
// text_layout +4 width, text_w = laid-out text width (clamped to box_w here),
// pad = glow padding. The caller subtracts x_shift from the state origin and
// pivot_shift from the state pivot; with an identity transform the texture
// then starts at origin.x - (x_shift - pivot_shift).
struct IgdtTextAnchor {
    int16_t x_shift;
    int32_t pivot_shift;
};
IgdtTextAnchor IgdtTextAnchorX(unsigned state_align, unsigned layout_align, uint16_t box_w, uint32_t pad,
                               uint32_t text_w);

#endif
