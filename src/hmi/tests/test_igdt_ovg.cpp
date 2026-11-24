// File name is ours; stock name unknown.
// Review Focus 3: a missing or corrupt _ovg.bin fails cleanly (false + log),
// never crashes or reads/writes past a buffer.
#include "display/igdt/igdt_ovg.h"
#include "tests/test_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>

#ifdef HMI_HAVE_IGDT_TABLES
#include "display/igdt/igdt_tables.h"
#endif

namespace {

// Failing loads log one line; announce it so the test output is readable.
#define CHECK_LOAD_FAILS(expr)                                   \
    do {                                                         \
        fprintf(stderr, "(expected failure follows) ");          \
        CHECK(!(expr));                                          \
    } while (0)

bool Rle(const std::vector<uint8_t>& in, size_t dst_len, int bpp, std::vector<uint8_t>& out) {
    // Canary byte after dst_len catches writes past the end.
    out.assign(dst_len + 1, 0xCD);
    bool ok = IgdtDecodeOvgRle(in.empty() ? 0 : &in[0], in.size(), &out[0], dst_len, bpp);
    CHECK(out[dst_len] == 0xCD);
    out.resize(dst_len);
    return ok;
}

std::vector<uint8_t> Bytes(const uint8_t* p, size_t n) { return std::vector<uint8_t>(p, p + n); }

void TestRle() {
    std::vector<uint8_t> out;

    const uint8_t run[] = {0x82, 0xAA};
    CHECK(Rle(Bytes(run, 2), 3, 1, out));
    CHECK(out.size() == 3 && out[0] == 0xAA && out[1] == 0xAA && out[2] == 0xAA);

    const uint8_t lit[] = {0x01, 0x10, 0x20};
    CHECK(Rle(Bytes(lit, 3), 2, 1, out));
    CHECK(out.size() == 2 && out[0] == 0x10 && out[1] == 0x20);

    // bpp 2: run of 2 pixels 0x1122, then literal of 1 pixel 0x3344.
    const uint8_t mixed[] = {0x81, 0x22, 0x11, 0x00, 0x44, 0x33};
    CHECK(Rle(Bytes(mixed, 6), 6, 2, out));
    const uint8_t want[] = {0x22, 0x11, 0x22, 0x11, 0x44, 0x33};
    CHECK(out == Bytes(want, 6));

    // Truncated: literal promises 2 bytes, only 1 present; run missing pixel.
    const uint8_t trunc_lit[] = {0x01, 0x10};
    CHECK(!Rle(Bytes(trunc_lit, 2), 2, 1, out));
    const uint8_t trunc_run[] = {0x82};
    CHECK(!Rle(Bytes(trunc_run, 1), 3, 1, out));

    // Short stream (stock): succeeds, unwritten tail is zero.
    CHECK(Rle(Bytes(run, 2), 4, 1, out));
    CHECK(out.size() == 4 && out[2] == 0xAA && out[3] == 0);
    CHECK(Rle(std::vector<uint8_t>(), 3, 1, out) && out[0] == 0 && out[2] == 0);

    // Overlong items are clamped at dst_len (stock); guard byte checked in Rle().
    CHECK(Rle(Bytes(run, 2), 2, 1, out));
    CHECK(out.size() == 2 && out[0] == 0xAA && out[1] == 0xAA);
    CHECK(Rle(Bytes(lit, 3), 1, 1, out) && out[0] == 0x10);
    // Run writes a partial last pixel (bpp 2, odd dst_len); literal is clamped.
    CHECK(Rle(Bytes(mixed, 6), 5, 2, out));
    const uint8_t want5[] = {0x22, 0x11, 0x22, 0x11, 0x44};
    CHECK(out == Bytes(want5, 5));
    CHECK(Rle(Bytes(mixed, 6), 3, 2, out));
    const uint8_t want3[] = {0x22, 0x11, 0x22};
    CHECK(out == Bytes(want3, 3));

    // Stream budget (src_len) ends decoding even though dst is not full, and the
    // consumed count stops at it.
    const uint8_t two_items[] = {0x80, 0x01, 0x80, 0x02};
    size_t used = 0;
    uint8_t d2[2] = {9, 9};
    CHECK(IgdtDecodeOvgRle(two_items, 2, d2, 2, 1, &used) && used == 2 && d2[0] == 1 && d2[1] == 0);

    // Bad bpp.
    uint8_t d[4];
    CHECK(!IgdtDecodeOvgRle(run, 2, d, 3, 0));
    CHECK(!IgdtDecodeOvgRle(run, 2, d, 3, 5));

    // consumed reports bytes read; trailing input is left alone.
    const uint8_t trailing[] = {0x82, 0xAA, 0x55, 0x55};
    uint8_t d3[3];
    CHECK(IgdtDecodeOvgRle(trailing, 4, d3, 3, 1, &used) && used == 2);

    // Longest run: 0xff = 128 repeats.
    const uint8_t longrun[] = {0xff, 0x07};
    std::vector<uint8_t> big;
    CHECK(Rle(Bytes(longrun, 2), 128, 1, big));
    CHECK(big.size() == 128 && big[0] == 7 && big[127] == 7);
}

void TestFormats() {
    // glTexImage2D format/type stock picks per tex_table fmt (FUN_00325728).
    unsigned fmt = 0, type = 0;
    CHECK(IgdtGlFormatType(IGDT_FMT_RGB565, &fmt, &type) && fmt == 0x1907 && type == 0x8363);
    CHECK(IgdtGlFormatType(IGDT_FMT_RGBA5551, &fmt, &type) && fmt == 0x1908 && type == 0x8034);
    CHECK(IgdtGlFormatType(IGDT_FMT_RGBA4444, &fmt, &type) && fmt == 0x1908 && type == 0x8033);
    CHECK(IgdtGlFormatType(IGDT_FMT_A8, &fmt, &type) && fmt == 0x1906 && type == 0x1401);
    CHECK(IgdtGlFormatType(IGDT_FMT_RGBA8888, &fmt, &type) && fmt == 0x1908 && type == 0x1401);
    CHECK(IgdtGlFormatType(IGDT_FMT_RGBA8888_B, &fmt, &type) && fmt == 0x1908 && type == 0x1401);
    CHECK(!IgdtGlFormatType(0x7e, &fmt, &type));

    CHECK(IgdtOvgBytesPerPixel(IGDT_FMT_RGBA8888) == 4 && IgdtOvgBytesPerPixel(IGDT_FMT_RGBA8888_B) == 4);
    CHECK(IgdtOvgBytesPerPixel(IGDT_FMT_RGB565) == 2 && IgdtOvgBytesPerPixel(IGDT_FMT_RGBA5551) == 2);
    CHECK(IgdtOvgBytesPerPixel(IGDT_FMT_RGBA4444) == 2 && IgdtOvgBytesPerPixel(IGDT_FMT_A8) == 1);
    CHECK(IgdtOvgBytesPerPixel(0x7e) == 0);
}

std::string TempPath(const char* name) {
    const char* dir = getenv("TMPDIR");
    std::string p = dir && *dir ? dir : "/tmp";
    if (p[p.size() - 1] != '/') {
        p += '/';
    }
    return p + name;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == 0) {
        return false;
    }
    bool ok = fwrite(&data[0], 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

void TestLoad() {
    std::vector<uint8_t> px(7, 0x11);
    unsigned fmt = 0, type = 0;

    // Missing file: false, empty output, no crash.
    IgdtTexEntry missing = {IGDT_FMT_RGBA8888, IGDT_FLAGS_RLE, 0, 0, 0, 2u, 1, 1, 0xff000000u,
                            "gresfiles/does_not_exist_ovg.bin"};
    CHECK_LOAD_FAILS(IgdtLoadOvgRaw(missing, "/nonexistent-root", px, &fmt, &type));
    CHECK(px.empty());
    CHECK_LOAD_FAILS(IgdtLoadOvgRaw(missing, 0, px, 0, 0));

    // Files live at <root>/gresfiles/<name>; root has no trailing slash here.
    std::string root = TempPath("hmi_ovg_test_root");
    std::string cmd = "mkdir -p '" + root + "/gresfiles'";
    CHECK(system(cmd.c_str()) == 0);
    std::string file = root + "/gresfiles/t_ovg.bin";

    // RLE RGB565: run of 2 pixels 0xF800 (LE bytes 00 F8), native bytes out.
    const uint8_t rle565[] = {0x81, 0x00, 0xF8};
    const uint8_t two_red[] = {0x00, 0xF8, 0x00, 0xF8};
    CHECK(WriteFile(file, Bytes(rle565, 3)));
    IgdtTexEntry e = {IGDT_FMT_RGB565, IGDT_FLAGS_RLE, 0, 0, 0, 3u, 2, 1, 0xff000000u,
                      "gresfiles/t_ovg.bin"};
    CHECK(IgdtLoadOvgRaw(e, root.c_str(), px, &fmt, &type));
    CHECK(px == Bytes(two_red, 4) && fmt == 0x1907 && type == 0x8363);
    // Trailing slash on root is fine.
    CHECK(IgdtLoadOvgRaw(e, (root + "/").c_str(), px, 0, 0) && px == Bytes(two_red, 4));

    // Stream shorter than w*h (3 pixels wanted, 2 encoded): loads, last pixel 0.
    e.w = 3;
    CHECK(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0));
    CHECK(px.size() == 6 && px[3] == 0xF8 && px[4] == 0 && px[5] == 0);
    // Stream longer than w*h (1 pixel wanted, run of 2): clamped.
    e.w = 1;
    CHECK(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0));
    CHECK(px.size() == 2 && px[0] == 0x00 && px[1] == 0xF8);
    // Stream ends inside an item (bsz promises more than the file holds): fails.
    e.w = 2;
    e.bsz = 4;
    const uint8_t cut[] = {0x01, 0x00};  // literal of 2 pixels, none present
    CHECK(WriteFile(file, Bytes(cut, 2)));
    CHECK_LOAD_FAILS(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0));
    CHECK(px.empty());
    CHECK(WriteFile(file, Bytes(rle565, 3)));
    // File shorter than bsz but stream complete: loads (stock stops when dst is full).
    CHECK(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0) && px == Bytes(two_red, 4));
    // File longer than the stream bsz declares: only bsz bytes are read, stream is complete.
    const uint8_t longer[] = {0x81, 0x00, 0xF8, 0x99};
    CHECK(WriteFile(file, Bytes(longer, 4)));
    e.bsz = 3;
    CHECK(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0) && px == Bytes(two_red, 4));
    // Unknown fmt / flags.
    e.fmt = 0x7e;
    CHECK_LOAD_FAILS(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0));
    e.fmt = IGDT_FMT_RGB565;
    e.flags = 5;
    CHECK_LOAD_FAILS(IgdtLoadOvgRaw(e, root.c_str(), px, 0, 0));
    e.flags = IGDT_FLAGS_RLE;

    // Raw (flags 0) loads return the file bytes and stock's GL format/type.
    const uint8_t a8file[] = {0x10, 0x20};
    CHECK(WriteFile(file, Bytes(a8file, 2)));
    IgdtTexEntry raw = {IGDT_FMT_A8, IGDT_FLAGS_RAW, 0, 0, 0, 2u, 2, 1, 0xff000000u,
                        "gresfiles/t_ovg.bin"};
    CHECK(IgdtLoadOvgRaw(raw, root.c_str(), px, &fmt, &type));
    CHECK(px == Bytes(a8file, 2) && fmt == 0x1906 && type == 0x1401);
    raw.fmt = IGDT_FMT_RGBA5551;
    raw.w = 1;
    CHECK(IgdtLoadOvgRaw(raw, root.c_str(), px, &fmt, &type));
    CHECK(px == Bytes(a8file, 2) && fmt == 0x1908 && type == 0x8034);
    raw.fmt = IGDT_FMT_RGBA4444;
    CHECK(IgdtLoadOvgRaw(raw, root.c_str(), px, 0, &type) && type == 0x8033);
    raw.fmt = IGDT_FMT_RGB565;
    CHECK(IgdtLoadOvgRaw(raw, root.c_str(), px, &fmt, &type) && fmt == 0x1907 && type == 0x8363);
    raw.fmt = IGDT_FMT_A8;
    raw.w = 3;  // raw file too short
    CHECK_LOAD_FAILS(IgdtLoadOvgRaw(raw, root.c_str(), px, 0, 0));
    CHECK(px.empty());

    cmd = "rm -rf '" + root + "'";
    CHECK(system(cmd.c_str()) == 0);
}

#ifdef HMI_HAVE_IGDT_TABLES
// Every unique gresfiles path (the slot with the largest w*h, per
// docs/assets-and-localization.md) must decode through the device loader
// (IgdtLoadOvgRaw) to exactly w*h*bpp native bytes with stock's GL
// format/type, consuming exactly bsz bytes. HMI_GRESFILES points at
// .../opt/gresfiles; tools/build_hmi.sh --host-tests always sets it (unless
// --no-assets, which also compiles this sweep out), so it is required here.
bool LoadsNative(const IgdtTexEntry& e, const std::string& opt, std::vector<uint8_t>& px) {
    unsigned fmt = 0, type = 0, want_fmt = 0, want_type = 0;
    return IgdtLoadOvgRaw(e, opt.c_str(), px, &fmt, &type) &&
           px.size() == (size_t)e.w * e.h * IgdtOvgBytesPerPixel(e.fmt) &&
           IgdtGlFormatType(e.fmt, &want_fmt, &want_type) && fmt == want_fmt && type == want_type;
}

void TestRealGresfilesSweep() {
    const char* gres = getenv("HMI_GRESFILES");
    CHECK(gres != 0 && *gres != '\0');
    if (gres == 0 || *gres == '\0') {
        fprintf(stderr, "real_gresfiles_sweep: HMI_GRESFILES not set (run through tools/build_hmi.sh --host-tests)\n");
        return;
    }
    std::string opt = gres;
    while (!opt.empty() && opt[opt.size() - 1] == '/') {
        opt.resize(opt.size() - 1);
    }
    std::string::size_type slash = opt.rfind('/');
    CHECK(slash != std::string::npos);
    opt = slash == std::string::npos ? "." : opt.substr(0, slash);

    std::map<std::string, const IgdtTexEntry*> best;
    for (unsigned i = 0; i < g_igdtTablesVw.tex_count; ++i) {
        const IgdtTexEntry& e = g_igdtTablesVw.textures[i];
        if (e.path == 0) {
            continue;
        }
        std::map<std::string, const IgdtTexEntry*>::iterator it = best.find(e.path);
        if (it == best.end() || (unsigned)e.w * e.h > (unsigned)it->second->w * it->second->h) {
            best[e.path] = &e;
        }
    }
    unsigned bad = 0;
    std::vector<uint8_t> px;
    for (std::map<std::string, const IgdtTexEntry*>::iterator it = best.begin(); it != best.end(); ++it) {
        const IgdtTexEntry& e = *it->second;
        if (!LoadsNative(e, opt, px)) {
            ++bad;
            continue;
        }
        if ((e.flags & 0x7f) == IGDT_FLAGS_RLE) {
            // The full-size slot's stream is exactly bsz bytes for w*h*bpp.
            std::vector<uint8_t> file(e.bsz), raw((size_t)e.w * e.h * IgdtOvgBytesPerPixel(e.fmt));
            FILE* f = fopen((opt + "/" + e.path).c_str(), "rb");
            size_t got = f ? fread(&file[0], 1, e.bsz, f) : 0;
            if (f) {
                fclose(f);
            }
            size_t used = 0;
            if (got != e.bsz || !IgdtDecodeOvgRle(&file[0], got, &raw[0], raw.size(), IgdtOvgBytesPerPixel(e.fmt), &used) ||
                used != e.bsz) {
                ++bad;
            }
        }
    }
    fprintf(stderr, "real_gresfiles_sweep: %u unique paths, %u bad\n", (unsigned)best.size(), bad);
    CHECK(best.size() == 2806);
    CHECK(bad == 0);

    // Every slot (clipped sub-rect slots included) loads with its own w/h. Stock
    // fails a raw (flags 0) slot whose file is shorter than w*h*bpp (3 slots
    // point at an RLE file); expect exactly that.
    unsigned slot_bad = 0, raw_short = 0;
    for (unsigned i = 0; i < g_igdtTablesVw.tex_count; ++i) {
        const IgdtTexEntry& e = g_igdtTablesVw.textures[i];
        if (LoadsNative(e, opt, px)) {
            continue;
        }
        long size = -1;
        FILE* f = fopen((opt + "/" + e.path).c_str(), "rb");
        if (f && fseek(f, 0, SEEK_END) == 0) {
            size = ftell(f);
        }
        if (f) {
            fclose(f);
        }
        if ((e.flags & 0x7f) == IGDT_FLAGS_RAW && size >= 0 &&
            (size_t)size < (size_t)e.w * e.h * IgdtOvgBytesPerPixel(e.fmt)) {
            ++raw_short;
        } else {
            ++slot_bad;
        }
    }
    fprintf(stderr, "real_gresfiles_sweep: %u slots, %u raw-short (stock fails too), %u bad\n",
            g_igdtTablesVw.tex_count, raw_short, slot_bad);
    CHECK(g_igdtTablesVw.tex_count == 32110);
    CHECK(slot_bad == 0);
}
#endif

} // namespace

int main() {
    TestRle();
    TestFormats();
    TestLoad();
#ifdef HMI_HAVE_IGDT_TABLES
    TestRealGresfilesSweep();
#endif
    return TEST_RESULT();
}
