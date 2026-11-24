// File name is ours; stock name unknown.
//
// IGDT FreeType text: the 48 font styles of 0x003cb25c (VWThesis MQB Light /
// Regular for Latin and Cyrillic, FZHT_GB18030 as the CJK fallback of style
// n in slot n + 24), loaded lazily as 0x0031d2a4 does; line layout and
// truncation (0x00329ea0); rasterization of a whole line into one A8 texture
// with FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT glyphs and the per-style
// baseline tweaks of 0x003278e0. The GL half of the draw is
// IgdtDrawTextCoverage (igdt_gl.h).
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_TEXT_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_TEXT_H

#include "display/igdt/igdt_tables.h"
#include "display/igdt/igdt_textbuf.h"

// Initializes FreeType; font files are font_root + "/" + the style's
// "gresfiles/....ttf" path (on the device font_root is "/opt"; NULL or ""
// opens the relative path as stock does). Faces load on first use. False
// (logged) when FreeType cannot be initialized. DEVIATION: stock calls
// FT_Init_FreeType lazily from 0x0031d2a4.
bool IgdtTextInit(const char* font_root);

// Releases every face and the FreeType library. DEVIATION: stock never does.
void IgdtTextTerm();

// 0x003278e0: draws the NUL-terminated text of txt element e (font, align,
// width from e's text_layout) at the current draw state, whose origin is the
// element's anchor (the scene walk sets it). Like stock it moves the state
// origin and pivot to place the line. Draws nothing when the state is
// transparent or clipped out, FreeType is not initialized, the style's face
// cannot be loaded, or the text lays out to width 0.
void IgdtDrawText(const IgdtElement& e, const IgdtWchar* text);

#endif
