// File name is ours; stock name unknown.
#include "display/igdt/igdt_ovg.h"

#include <stdio.h>
#include <string.h>
#include <string>

#if defined(__arm__)
#include "service/osal/osal_api.h"
#endif

namespace {

// Largest texture we accept; stock tex_table tops out far below this.
const size_t kMaxTextureBytes = 64u * 1024u * 1024u;
// Largest RLE item: control byte + 128 pixels is a literal of 1 + 128 * 4.
const size_t kMaxRleItemBytes = 1 + 128 * 4;

// DEVIATION: stock has no failure log here (FUN_00325728 just returns 0);
// brief requires one line with the path. Device: libOSAL log_simple, host
// tests: stderr.
void LogLoadFailure(const char* path, const char* why) {
#if defined(__arm__)
    log_simple("IGDT", "ovg load failed: %s (%s)\n", path, why);
#else
    fprintf(stderr, "IGDT: ovg load failed: %s (%s)\n", path, why);
#endif
}

bool ReadFile(const std::string& path, size_t want, std::vector<uint8_t>& out) {
    out.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (f == 0) {
        return false;
    }
    out.resize(want);
    size_t got = want == 0 ? 0 : fread(&out[0], 1, want, f);
    fclose(f);
    if (got != want) {
        out.clear();
        return false;
    }
    return true;
}

// Reads up to max_bytes from the start of the file (fewer if it is shorter);
// false only if it cannot be opened.
bool ReadFilePrefix(const std::string& path, size_t max_bytes, std::vector<uint8_t>& out) {
    out.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (f == 0) {
        return false;
    }
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size >= 0 && (size_t)size < max_bytes) {
            max_bytes = (size_t)size;
        }
        fseek(f, 0, SEEK_SET);
    } else if (max_bytes > kMaxTextureBytes) {
        max_bytes = kMaxTextureBytes;
    }
    out.resize(max_bytes);
    size_t got = max_bytes == 0 ? 0 : fread(&out[0], 1, max_bytes, f);
    fclose(f);
    out.resize(got);
    return true;
}

} // namespace

int IgdtOvgBytesPerPixel(uint8_t fmt) {
    switch (fmt) {
    case IGDT_FMT_RGBA8888:
    case IGDT_FMT_RGBA8888_B:
        return 4;
    case IGDT_FMT_RGB565:
    case IGDT_FMT_RGBA5551:
    case IGDT_FMT_RGBA4444:
        return 2;
    case IGDT_FMT_A8:
        return 1;
    }
    return 0;
}

// 0x00325728 FUN_00325728 (inline RLE loop)
// Symbol file labels 0x00323dfc IGDT_DecodeOvgRle, but that body is a scene
// state updater; the PackBits loop is inside 0x00325728.
// Stock semantics reproduced: items are read until dst is full or the stream
// budget (stock: bsz, decremented 1 + bytes read per item) is used up, so a
// short stream succeeds and leaves the rest of dst unwritten; an overlong run
// or literal is clamped at the end of dst (a run may write a partial pixel,
// a literal reads only the clamped byte count); an item whose bytes cannot be
// read (EOF) fails the whole decode.
// STOCK-QUIRK: stock lets the last item read past the budget; here avail
// bounds all reads and budget only decides when to stop.
// DEVIATION: deterministic fill: stock's malloc'd buffer is uninitialized
// where a short stream leaves it; we zero all of dst first.
static bool DecodeRle(const uint8_t* src, size_t avail, size_t budget, uint8_t* dst,
                      size_t dst_len, int bpp, size_t* consumed) {
    if (bpp < 1 || bpp > 4) {
        return false;
    }
    if (dst_len != 0) {
        memset(dst, 0, dst_len);
    }
    size_t in = 0;
    size_t out = 0;
    const size_t px = (size_t)bpp;
    while (out < dst_len && in < budget) {
        if (in >= avail) {
            return false;
        }
        const unsigned ctrl = src[in++];
        if (ctrl & 0x80u) {
            const size_t count = (size_t)(ctrl & 0x7fu) + 1;
            if (avail - in < px) {
                return false;
            }
            const uint8_t* pixel = src + in;
            in += px;
            for (size_t i = 0; i < count && out < dst_len; ++i) {
                const size_t n = dst_len - out < px ? dst_len - out : px;
                memcpy(dst + out, pixel, n);
                out += n;
            }
        } else {
            const size_t room = dst_len - out;
            size_t bytes = ((size_t)ctrl + 1) * px;
            if (bytes > room) {
                bytes = room;
            }
            if (avail - in < bytes) {
                return false;
            }
            memcpy(dst + out, src + in, bytes);
            out += bytes;
            in += bytes;
        }
    }
    if (consumed != 0) {
        *consumed = in;
    }
    return true;
}

// 0x00325728 FUN_00325728 (inline RLE loop; budget == src_len)
bool IgdtDecodeOvgRle(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, int bpp,
                      size_t* consumed) {
    return DecodeRle(src, src_len, src_len, dst, dst_len, bpp, consumed);
}

// 0x00325728 FUN_00325728 (GL format/type selection)
bool IgdtGlFormatType(uint8_t fmt, unsigned* gl_format, unsigned* gl_type) {
    switch (fmt) {
    case IGDT_FMT_RGBA8888:
    case IGDT_FMT_RGBA8888_B:
        *gl_format = 0x1908; *gl_type = 0x1401; return true;   // GL_RGBA, GL_UNSIGNED_BYTE
    case IGDT_FMT_RGB565:
        *gl_format = 0x1907; *gl_type = 0x8363; return true;   // GL_RGB, ..._5_6_5
    case IGDT_FMT_RGBA5551:
        *gl_format = 0x1908; *gl_type = 0x8034; return true;   // GL_RGBA, ..._5_5_5_1
    case IGDT_FMT_RGBA4444:
        *gl_format = 0x1908; *gl_type = 0x8033; return true;   // GL_RGBA, ..._4_4_4_4
    case IGDT_FMT_A8:
        *gl_format = 0x1906; *gl_type = 0x1401; return true;   // GL_ALPHA, GL_UNSIGNED_BYTE
    }
    return false;
}

// 0x00325728 FUN_00325728
// DEVIATION: fail instead of uninitialized texture (stock returns success for
// an unknown fmt without creating the texture data).
bool IgdtLoadOvgRaw(const IgdtTexEntry& e, const char* root, std::vector<uint8_t>& out,
                    unsigned* gl_format, unsigned* gl_type) {
    out.clear();
    const char* path = e.path != 0 ? e.path : "(null)";
    const int bpp = IgdtOvgBytesPerPixel(e.fmt);
    if (e.path == 0) {
        LogLoadFailure(path, "no path");
        return false;
    }
    unsigned format = 0, type = 0;
    if (bpp == 0 || !IgdtGlFormatType(e.fmt, &format, &type)) {
        LogLoadFailure(path, "unknown fmt");
        return false;
    }
    const unsigned flags = e.flags & 0x7fu;
    if (flags != IGDT_FLAGS_RAW && flags != IGDT_FLAGS_RLE) {
        LogLoadFailure(path, "unknown flags");
        return false;
    }
    const uint64_t raw_bytes = (uint64_t)e.w * e.h * (uint64_t)bpp;
    if (raw_bytes > kMaxTextureBytes) {
        LogLoadFailure(path, "texture too large");
        return false;
    }

    std::string full;
    if (root != 0 && root[0] != '\0') {
        full = root;
        if (full[full.size() - 1] != '/') {
            full += '/';
        }
    }
    full += e.path;

    std::vector<uint8_t>& raw = out;
    if (flags == IGDT_FLAGS_RAW) {
        if (!ReadFile(full, (size_t)raw_bytes, raw)) {
            LogLoadFailure(path, "missing or short file");
            return false;
        }
    } else {
        // Stock reads item by item and may run past bsz by one item
        // (at most 1 + 128 * 4 bytes), so allow that much slack.
        std::vector<uint8_t> file;
        const size_t budget = e.bsz > kMaxTextureBytes ? kMaxTextureBytes : (size_t)e.bsz;
        if (!ReadFilePrefix(full, budget + kMaxRleItemBytes, file)) {
            LogLoadFailure(path, "missing file");
            return false;
        }
        raw.resize((size_t)raw_bytes);
        if (!DecodeRle(file.empty() ? 0 : &file[0], file.size(), budget, raw.empty() ? 0 : &raw[0],
                       raw.size(), bpp, 0)) {
            out.clear();
            LogLoadFailure(path, "RLE stream ends inside an item");
            return false;
        }
    }

    if (gl_format != 0) {
        *gl_format = format;
    }
    if (gl_type != 0) {
        *gl_type = type;
    }
    return true;
}
