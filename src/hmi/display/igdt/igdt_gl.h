// File name is ours; stock name unknown.
//
// IGDT GLES2 device layer: the EGL/GLES2 backend whose entry points sit in
// the 0x00453650 function table (init 0x00326084, frame begin 0x00326a50,
// image 0x00326ee8, swap 0x00326b3c). Stock renders every element into an
// 800x480 FBO texture and copies that to the window surface on swap; this
// layer issues the same fb*/egl*/gl* calls in the same order, so on QEMU
// (tools/arm_gl/gl_fb_shim.c) it produces the same pixels.
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_GL_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_GL_H

#include <stddef.h>
#include <stdint.h>

#include "display/igdt/igdt_tables.h"

typedef unsigned int GLuint;

// 0x00a0f908 draw state (current origin, alpha, tint, transform and clip
// that the scene walk accumulates and the device layer draws with). Field
// offsets are stock's.
struct IgdtDrawState {
    int16_t x, y;                  // +0x00 origin
    uint8_t alpha;                 // +0x04 0..255
    uint32_t tint;                 // +0x08 0xffffffff = none, else 0x..RRGGBB
    uint8_t flags;                 // +0x0c bit0 transparent, bit1 clipped out
    float matrix[9];               // +0x10 3x3 transform, column-major
    int16_t clip_x0, clip_y0;      // +0x34 clip rect, screen pixels
    int16_t clip_x1, clip_y1;      // +0x38 (exclusive)
    uint8_t text_align;            // +0x3f text: bits 0-1 horizontal align, bits 2-3
                                   //       background off unless 0, bits 4-5 truncation
    uint8_t font;                  // +0x40 text font style 0..47
    uint16_t screen_w, screen_h;   // +0x44 display size
    uint32_t fg_color;             // +0x4c text colour 0xAARRGGBB
    uint32_t bg_color;             // +0x50 text background colour 0xAARRGGBB
    float pivot_x, pivot_y;        // +0x54 transform pivot
};

enum { IGDT_STATE_TRANSPARENT = 0x01, IGDT_STATE_CLIPPED = 0x02 };

// 0x0031d6fc: the draw state the scene walk fills and the device layer reads.
IgdtDrawState& IgdtCurrentState();

// 0x00326084: Vivante fb window + EGL surface/context, the five stock shader
// programs, the w x h FBO texture and the shared vertex buffer. False when
// EGL or a shader fails (the reason is logged).
bool IgdtGlInit(int w, int h);

// Releases what IgdtGlInit and IgdtTexture created. DEVIATION: stock never
// tears the GL state down.
void IgdtGlTerm();

// GL texture of t.textures[tex_index], uploaded on first use in its native
// format exactly as 0x00325728 does and cached per (table, index) afterwards.
// 0 when the index is out of range or the _ovg.bin cannot be loaded (logged
// once by the loader; never retried). program (may be NULL) receives the
// shader program stock pairs with the texture's format.
GLuint IgdtTexture(const IgdtSkinTables& t, int tex_index, GLuint* program);

// 0x00326a50: binds the FBO, enables blending and a full-screen scissor.
void IgdtBeginFrame();
// Clears the whole render target to transparent black (ours, see
// IgdtSceneDrawLayers).
void IgdtClearFrame();

// 0x00326ee8: draws t.textures[e.tex] (e.w x e.h pixels, via IgdtTexture) at the
// current draw state's transform with its alpha, tint and clip. Like stock it
// updates the scissor and MVP first; nothing is drawn when the state is
// transparent or clipped out, or the texture cannot be loaded.
void IgdtDrawImage(const IgdtSkinTables& t, const IgdtElement& e);

// One rasterized text line for IgdtDrawTextCoverage.
struct IgdtTextCoverage {
    const uint8_t* alpha;          // w * h 8-bit coverage, row stride w
    uint16_t w, h;                 // texture size: line width x line height
    uint32_t fill_w, fill_h;       // background box (glow pad + clamped width, line height + pad)
    const uint32_t* fg_escapes;    // 0x01 colour escapes met while rasterizing, as
    size_t fg_escape_count;        // (pw[1], pw[2]) pairs (0x00325c84), in order
};

// 0x003278e0 (GL part of the FreeType branch): with the current draw state
// (origin already placed by IgdtDrawText) sets scissor and MVP, fills the
// background box with state.bg_color when state.text_align bits 2-3 are 0,
// then draws c.alpha as a GL_ALPHA texture with the font program
// (fgColor = state.fg_color, Alpha = state alpha). DEVIATION: the texture is
// deleted again right after the draw (stock keeps it in its line cache).
void IgdtDrawTextCoverage(const IgdtTextCoverage& c);

// 0x0032749c: w x h rect of color (alpha scaled by the state alpha) at the
// current draw state's transform, clipped to its clip; nothing when the
// state is transparent or clipped out. A rect the size of the screen clears
// the clip area to color instead (0x00327408), as stock.
void IgdtDrawRect(uint16_t w, uint16_t h, uint32_t color);

// 0x00326b3c: copies the FBO to the window surface and swaps.
void IgdtSwap();

// True once IgdtSwap swapped the first frame and sent wake state 2 (stock
// flag 0x0096f1cc); VControlTask then sends it itself on the 0x81 audio mode.
bool IgdtFirstFrameSwapped();

#endif
