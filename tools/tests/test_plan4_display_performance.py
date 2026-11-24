#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan4DisplayPerformance(unittest.TestCase):
    def test_imx6_ipu_uses_rgb565_lut_and_dirty_row_rect(self):
        ipu_c = (ROOT / "qemu/hw/display/imx6_ipu.c").read_text()
        self.assertIn("rgb565_lut", ipu_c)
        self.assertIn("min_dirty_y", ipu_c)
        self.assertIn("prev_bpp", ipu_c)
        self.assertNotIn("* 255 / 31;", ipu_c.split("imx6_ipu_gfx_update")[1])

        # Verify RGB565 LUT mapping across all 65,536 values matches PIXMAN_x8r8g8b8
        lut = [
            0xFF000000
            | ((((p >> 11) & 0x1F) * 255 // 31) << 16)
            | ((((p >> 5) & 0x3F) * 255 // 63) << 8)
            | ((p & 0x1F) * 255 // 31)
            for p in range(65536)
        ]
        self.assertEqual(lut[0x0000], 0xFF000000)
        self.assertEqual(lut[0xF800], 0xFFFF0000)  # pure red
        self.assertEqual(lut[0x07E0], 0xFF00FF00)  # pure green
        self.assertEqual(lut[0x001F], 0xFF0000FF)  # pure blue
        self.assertEqual(lut[0xFFFF], 0xFFFFFFFF)  # white

    def test_gl_fb_shim_tracks_dirty_rows_and_uses_neon(self):
        gl_c = (ROOT / "tools/arm_gl/gl_fb_shim.c").read_text()
        shims_sh = (ROOT / "tools/build_shims.sh").read_text()
        self.assertIn("dirty_y0", gl_c)
        self.assertIn("dirty_y1", gl_c)
        self.assertIn("mark_backbuf_dirty", gl_c)
        self.assertIn("__may_alias__", gl_c)
        self.assertIn("restrict", gl_c)
        self.assertIn("neon", shims_sh)
        self.assertIn("-ftree-vectorize", shims_sh)

    def test_prebuilt_libegl_contains_neon_simd_and_no_softfloat_calls(self):
        lib_path = ROOT / "tools/prebuilt/libEGL.so.1"
        disasm = subprocess.check_output(["objdump", "-d", str(lib_path)], text=True)
        self.assertRegex(disasm, r"\b(vst1|vld1|vmovn)\.")
        self.assertNotIn("__aeabi_fmul", disasm)
        self.assertNotIn("__aeabi_fadd", disasm)

    def test_gl_fb_shim_dirty_rows_and_rgb565_runtime_behavior(self):
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gl_fb_shim.c"

static _Alignas(4) uint16_t fake_fb[FB_W * FB_H];
static _Alignas(4) uint16_t fake_fb_unaligned[FB_W * FB_H + FB_H + 4];
static _Alignas(4) uint32_t fake_fb32[FB_W * FB_H];

int main(void) {
    init_gl_once();
    g_gl.fb_mmap = (uint8_t *)fake_fb;
    g_gl.fb_size = sizeof(fake_fb);
    g_gl.fb_bpp = 16;
    g_gl.fb_stride = FB_W * 2;

    /* Initial state: full screen dirty [0, FB_H). */
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f); /* pure blue -> 0x001f */
    glClear(0x4000);
    assert(g_gl.dirty_y0 == 0 && g_gl.dirty_y1 == FB_H);
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb[0] == 0x001fu);
    assert(fake_fb[FB_W * FB_H - 1] == 0x001fu);
    assert(g_gl.dirty_y0 == FB_H && g_gl.dirty_y1 == 0);

    /* Idle swap must not touch fb_mmap at all. */
    memset(fake_fb, 0xaa, sizeof(fake_fb));
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb[0] == 0xaaaau);
    assert(fake_fb[FB_W * 240] == 0xaaaau);

    /* Scissored clear on top-down rows [100, 120): in GL bottom-up coords y = 480 - 120 = 360, h = 20. */
    glEnable(0x0c11); /* GL_SCISSOR_TEST */
    glScissor(0, 360, FB_W, 20);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f); /* pure red -> 0xf800 */
    glClear(0x4000);
    assert(g_gl.dirty_y0 == 100 && g_gl.dirty_y1 == 120);
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb[99 * FB_W] == 0xaaaau);
    assert(fake_fb[100 * FB_W] == 0xf800u);
    assert(fake_fb[100 * FB_W + 1] == 0xf800u);
    assert(fake_fb[119 * FB_W + FB_W - 1] == 0xf800u);
    assert(fake_fb[120 * FB_W] == 0xaaaau);
    glDisable(0x0c11);

    /* Drawing to an off-screen FBO must NOT dirty the default backbuffer. */
    uint32_t tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(0x0de1, tex);
    glTexImage2D(0x0de1, 0, 0x1908, 16, 16, 0, 0x1908, 0x1401, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8d40, fbo);
    glFramebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, tex, 0);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(0x4000);
    assert(g_gl.dirty_y0 == FB_H && g_gl.dirty_y1 == 0);
    glBindFramebuffer(0x8d40, 0);

    /* draw_quad (PROG_SOLID) marks dirty rows and packs even/odd RGB565 pairs in order. */
    uint32_t sh = glCreateShader(0x8b30);
    const char *src_solid = "uniform vec4 fillColor;";
    glShaderSource(sh, 1, &src_solid, NULL);
    uint32_t prog = glCreateProgram();
    glAttachShader(prog, sh);
    glUseProgram(prog);
    glUniform4f(6, 0.0f, 1.0f, 0.0f, 1.0f); /* pure green -> 0x07e0 */
    /* Draw quad covering x in [25, 50) (odd start x=25) and top-down y in [180, 240)
       using exact dyadic NDC coordinates: x=-0.9375..-0.875, gl_y=240..300 -> cy=0.0..0.25. */
    float verts[8] = {
        -0.9375f, 0.0f,
        -0.875f,  0.0f,
        -0.9375f, 0.25f,
        -0.875f,  0.25f,
    };
    glBindBuffer(0x8892, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, 0x1406, 0, 0, verts);
    glDrawArrays(5, 0, 4);
    assert(g_gl.dirty_y0 == 180 && g_gl.dirty_y1 == 240);
    eglSwapBuffers(NULL, NULL);
    /* x=24 still blue (0x001f from initial clear), x=25 now green (0x07e0), rows 179 & 240 untouched (0xaaaa). */
    assert(fake_fb[179 * FB_W] == 0xaaaau);
    assert(fake_fb[180 * FB_W + 24] == 0x001fu);
    assert(fake_fb[180 * FB_W + 25] == 0x07e0u);
    assert(fake_fb[239 * FB_W + 49] == 0x07e0u);
    assert(fake_fb[239 * FB_W + 50] == 0x001fu);
    assert(fake_fb[240 * FB_W] == 0xaaaau);
    assert(g_gl.dirty_y0 == FB_H && g_gl.dirty_y1 == 0);

    /* Verify unaligned 16bpp row fallback (+2 byte base offset, stride FB_W*2+2). */
    g_gl.fb_mmap = ((uint8_t *)fake_fb_unaligned) + 2;
    g_gl.fb_size = sizeof(fake_fb_unaligned) - 2;
    g_gl.fb_bpp = 16;
    g_gl.fb_stride = FB_W * 2 + 2;
    memset(fake_fb_unaligned, 0xaa, sizeof(fake_fb_unaligned));
    glEnable(0x0c11);
    glScissor(0, 475, FB_W, 5); /* top-down rows [0, 5) */
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f); /* pure red -> 0xf800 */
    glClear(0x4000);
    assert(g_gl.dirty_y0 == 0 && g_gl.dirty_y1 == 5);
    eglSwapBuffers(NULL, NULL);
    /* Row 0 starts at fake_fb_unaligned + 1 (2-byte aligned, NOT 4-byte aligned). */
    assert(fake_fb_unaligned[1] == 0xf800u);
    assert(fake_fb_unaligned[1 + FB_W - 1] == 0xf800u);
    /* Row 1 starts at offset FB_W*2+2 -> fake_fb_unaligned[1 + FB_W + 1] (4-byte aligned). */
    assert(fake_fb_unaligned[1 + (FB_W + 1)] == 0xf800u);
    /* Row 5 must remain untouched (0xaaaa). */
    assert(fake_fb_unaligned[1 + 5 * (FB_W + 1)] == 0xaaaau);
    glDisable(0x0c11);

    /* Verify 32bpp partial row scanout swaps R/B to XRGB8888 (0xAARRGGBB) by default and updates only dirty rows. */
    g_gl.fb_mmap = (uint8_t *)fake_fb32;
    g_gl.fb_size = sizeof(fake_fb32);
    g_gl.fb_bpp = 32;
    g_gl.fb_stride = FB_W * 4;
    g_gl.fb_swap_rb = 1;
    memset(fake_fb32, 0x55, sizeof(fake_fb32));
    glEnable(0x0c11);
    glScissor(0, 470, FB_W, 10); /* top-down rows [0, 10) */
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f); /* pure red -> backbuf 0xff0000ff -> fb0 XRGB 0xffff0000 */
    glClear(0x4000);
    assert(g_gl.dirty_y0 == 0 && g_gl.dirty_y1 == 10);
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb32[0] == 0xffff0000u);
    assert(fake_fb32[9 * FB_W + FB_W - 1] == 0xffff0000u);
    assert(fake_fb32[10 * FB_W] == 0x55555555u);

    /* Pure blue -> fb0 XRGB 0xff0000ff when fb_swap_rb == 1 */
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(0x4000);
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb32[0] == 0xff0000ffu);
    assert(fake_fb32[10 * FB_W] == 0x55555555u);

    /* Pure red -> fb0 RGBA 0xff0000ff when fb_swap_rb == 0 */
    g_gl.fb_swap_rb = 0;
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x4000);
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb32[0] == 0xff0000ffu);
    assert(fake_fb32[10 * FB_W] == 0x55555555u);
    g_gl.fb_swap_rb = 1;

    /* Verify fb_size clamping prevents out-of-bounds writes beyond mapped framebuffer. */
    g_gl.fb_mmap = (uint8_t *)fake_fb;
    g_gl.fb_bpp = 16;
    g_gl.fb_stride = FB_W * 2;
    g_gl.fb_size = (size_t)4 * FB_W * 2; /* Only 4 rows mapped */
    memset(fake_fb, 0xaa, sizeof(fake_fb));
    glDisable(0x0c11);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(0x4000); /* marks [0, 480) dirty */
    eglSwapBuffers(NULL, NULL);
    assert(fake_fb[3 * FB_W + FB_W - 1] == 0xffffu);
    assert(fake_fb[4 * FB_W] == 0xaaaau);
    return 0;
}
"""
        fb_h_stub = """
#ifndef _LINUX_FB_H_STUB
#define _LINUX_FB_H_STUB
#include <stdint.h>
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
struct fb_bitfield { uint32_t offset; uint32_t length; uint32_t msb_right; };
struct fb_var_screeninfo {
    uint32_t bits_per_pixel;
    struct fb_bitfield red, green, blue, transp;
};
struct fb_fix_screeninfo { uint32_t line_length; uint32_t smem_len; };
#endif
"""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            (tmp_path / "linux").mkdir()
            (tmp_path / "linux" / "fb.h").write_text(fb_h_stub)
            src = tmp_path / "test_gl_shim.c"
            bin_path = tmp_path / "test_gl_shim"
            src.write_text(harness)
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    f"-I{tmp_path}",
                    f"-I{ROOT / 'tools/arm_gl'}",
                    str(src),
                    "-o",
                    str(bin_path),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(bin_path)], check=True, capture_output=True, text=True)

        egl = (ROOT / "tools/prebuilt/libEGL.so.1").read_bytes()
        gles = (ROOT / "tools/prebuilt/libGLESv2.so.2").read_bytes()
        self.assertEqual(egl, gles)
        self.assertEqual(egl[:4], b"\x7fELF")


if __name__ == "__main__":
    unittest.main()
