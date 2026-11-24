// File name is ours; stock name unknown.
//
// IGDT texture loader: reads a gresfiles/*_ovg.bin (raw or PackBits-RLE
// pixels, no header) into its native pixel format. Stock does this inside the
// GL texture upload (FUN_00325728) and hands the native 565/4444/5551/A8/8888
// data to glTexImage2D; so do we (igdt_gl.cpp). The decode is split out here
// so it is host-testable.
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_OVG_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_OVG_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "display/igdt/igdt_tables.h"

// tex_table fmt bytes (IgdtTexEntry::fmt). From the glTexImage2D type chosen
// in FUN_00325728: 0x03 -> GL_UNSIGNED_SHORT_5_6_5, 0x44 -> ..._5_5_5_1
// (0x8034), 0x45 -> ..._4_4_4_4 (0x8033), 0x0b -> GL_ALPHA/UNSIGNED_BYTE,
// 0x40/0x41 -> GL_RGBA/UNSIGNED_BYTE. (docs/assets-and-localization.md has
// 0x44/0x45 swapped.)
enum {
    IGDT_FMT_A8 = 0x0b,
    IGDT_FMT_RGB565 = 0x03,
    IGDT_FMT_RGBA8888_B = 0x40,
    IGDT_FMT_RGBA8888 = 0x41,
    IGDT_FMT_RGBA5551 = 0x44,
    IGDT_FMT_RGBA4444 = 0x45
};

// IgdtTexEntry::flags & 0x7f
enum { IGDT_FLAGS_RAW = 0, IGDT_FLAGS_RLE = 2 };

// Bytes per pixel of a tex_table fmt, 0 for an unknown fmt.
int IgdtOvgBytesPerPixel(uint8_t fmt);

// Decodes a PackBits stream like stock FUN_00325728: ctrl & 0x80 -> one pixel
// (bpp bytes) repeated (ctrl & 0x7f) + 1 times; otherwise (ctrl + 1) * bpp
// literal bytes. Items are read until dst_len bytes are written or src_len
// stream bytes are consumed. An overlong run/literal is clamped at dst_len; a
// stream that ends early succeeds, leaving the unwritten tail zero (stock
// leaves it uninitialized). Returns false only when an item's bytes run past
// src_len (stock: fread failure) or bpp is not 1..4. Never reads past src_len
// or writes past dst_len. consumed (if non-NULL) receives the bytes read.
bool IgdtDecodeOvgRle(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, int bpp,
                      size_t* consumed = 0);

// Loads e.path under root and writes the decoded pixels in the native fmt
// (e.w * e.h * bpp bytes, 16-bit pixels little-endian) to out, plus the
// glTexImage2D format/type stock passes for that fmt (GL enum values, no GL
// headers needed), so the GL upload is byte-for-byte what stock does.
// gl_format/gl_type may be NULL. The file is root + "/" + e.path (e.path
// starts with "gresfiles/"; on the device root is "/opt", locally
// extracted-fw/5624_vw/opt); a NULL or empty root uses e.path as is, like
// stock's relative fopen. Returns false (out left empty) after one log line
// naming the path where stock fails: file missing, raw file shorter than
// w*h*bpp, RLE stream ending inside an item, unknown fmt/flags, texture too
// large. Short or overlong RLE streams load as on stock (see above).
bool IgdtLoadOvgRaw(const IgdtTexEntry& e, const char* root, std::vector<uint8_t>& out,
                    unsigned* gl_format, unsigned* gl_type);

// glTexImage2D format/type for a tex_table fmt; false for an unknown fmt.
bool IgdtGlFormatType(uint8_t fmt, unsigned* gl_format, unsigned* gl_type);

#endif
