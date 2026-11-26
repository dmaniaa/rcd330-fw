# Emulator Plan 2: GLES2 Software Shim (`gl_fb_shim.c`) IGDT Rendering Fidelity

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close all four rendering gaps in `tools/arm_gl/gl_fb_shim.c` exposed by decompiling IGDT (`0x31b000..0x32a500`) — `GL_REPEAT` tiling, rotated/skewed quad rasterization, `glBlendFuncSeparate` alpha blending, and the `PROG_BLUR` text-glow shader — verified by a fast host C pixel-test harness before rebuilding the ARMv7 NEON prebuilt shim.

**Architecture:** Today `gl_fb_shim.c` only supports axis-aligned bounding-box quads (`minx..maxx`, `miny..maxy`), ignores `glTexParameteri` (breaking IGDT vtable `[6]` `0x327698` tiled draws), ignores `glBlendFuncSeparate` (breaking IGDT's `SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE` FBO alpha accumulation), and passes `PROG_BLUR` straight through without applying `kernel_data[30]` glow (`blur_fs @ 0x8d4bc0`). We first add a host-compiled unit-test harness that links `gl_fb_shim.c` directly (with a test hook reading `g_gl.backbuf` and FBO textures), then implement each missing feature via strict pixel-level TDD while preserving the existing NEON fast paths for axis-aligned untinted/1:1 quads.

**Tech Stack:** C99 (`tools/arm_gl/gl_fb_shim.c`, `tools/tests/test_gl_fb_shim.c`), Bash (`tools/build_shims.sh`), Python 3 (`tools/tests/test_emu_plan2_gl_fb_shim.py`).

**Spec:** `docs/hmi-internals.md` (§1, IGDT shaders `0x8d4bc0..0x8d59c4`), `docs/decompiled/5624/hmi/functions/0x327698.c`, `docs/decompiled/5624/hmi/functions/0x326b3c.c`.

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- `tools/arm_gl/gl_fb_shim.c` must remain a single self-contained C file compiling cleanly with both host `cc`/`clang` (for unit tests) and ARMv7 Cortex-A9 NEON softfp (`tools/build_shims.sh`, zero external library dependencies beyond libc/libm, zero softfloat calls in `tools/tests/test_plan4_display_performance.py`).
- Preserve the existing `dirty_y0`/`dirty_y1` tracking and fast paths (`PROG_SOLID`, `PROG_FONT`, 1:1 `PROG_TEX_SKIN`) for axis-aligned quads so frame rate never regresses on the common case.
- Rebuild and update `tools/prebuilt/libEGL.so.1` and `tools/prebuilt/libGLESv2.so.2` via `./tools/build_shims.sh --force --update-prebuilt` after final edits, and ensure `test_plan4_display_performance.py` still passes.

## Review Focus

- **Degenerate or zero-area rotated quads**: A rotated quad whose vertices are collinear or out-of-bounds must exit cleanly without division by zero (`det == 0`) or out-of-bounds writes into `dst`; pinned in Task 3's test.
- **Negative or large `GL_REPEAT` UVs (`u < 0` or `u > 10.0`)**: Modulo wrapping for `GL_REPEAT` must handle negative coordinates (`((x % n) + n) % n`) cleanly so UVs never index outside `[0, tw-1]` / `[0, th-1]`; pinned in Task 2's test.
- **Additive FBO alpha overflow (`GL_ONE, GL_ONE`)**: When IGDT blends multiple translucent layers into an RGBA FBO with `glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE)`, destination alpha must saturate at `255` without wrapping an 8-bit channel; pinned in Task 4's test.
- **`PROG_BLUR` edge clamping & large/zero `kernel_radius`**: `blur_fs @ 0x8d4bc0` uses `clampedSample()` returning `(0,0,0,0)` outside `[0, 1]` and clamps `kernel_radius` against the 30-matrix (`480` float) `kernel_data` table; invalid `kernel_radius <= 0` or `> 30` must not overflow `kernel_data`; pinned in Task 5's test.
- **Host vs Linux `<linux/fb.h>` portability**: `gl_fb_shim.c` includes `<linux/fb.h>` on Linux; guarding the `/dev/fb0` block behind `#ifdef __linux__` (or `#ifndef GL_FB_SHIM_HOST_TEST`) keeps host tests runnable on both macOS and Linux without breaking ARM cross-compilation; pinned in Task 1's test.

---

### Task 1: Add Host C Pixel-Test Harness for `tools/arm_gl/gl_fb_shim.c`

**Files:**
- Modify: `tools/arm_gl/gl_fb_shim.c:10-25,209-239`
- Create: `tools/tests/test_gl_fb_shim.c`
- Create: `tools/tests/test_emu_plan2_gl_fb_shim.py`

**Interfaces:**
- Consumes: `gl_fb_shim.c` EGL/GLES2 entry points.
- Produces:
  - `#ifdef GL_FB_SHIM_HOST_TEST` build guard in `gl_fb_shim.c` exposing `void gl_fb_shim_test_reset(void)` and `const uint32_t *gl_fb_shim_test_backbuf(void)` (and skipping `<linux/fb.h>` / `/dev/fb0` ioctls when compiled on host macOS/Linux).
  - `tools/tests/test_gl_fb_shim.c` standalone C test suite compiled and executed by `tools/tests/test_emu_plan2_gl_fb_shim.py`.

- [ ] **Step 1: Write the failing Python test runner `tools/tests/test_emu_plan2_gl_fb_shim.py` and baseline `tools/tests/test_gl_fb_shim.c`**

`tools/tests/test_emu_plan2_gl_fb_shim.py`:
```python
#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestEmuPlan2GlFbShim(unittest.TestCase):
    def test_host_pixel_suite(self):
        with tempfile.TemporaryDirectory() as td:
            exe = pathlib.Path(td) / "test_gl_fb_shim"
            cmd = [
                "cc", "-std=c99", "-O2", "-Wall", "-Wextra",
                "-DGL_FB_SHIM_HOST_TEST",
                str(ROOT / "tools/arm_gl/gl_fb_shim.c"),
                str(ROOT / "tools/tests/test_gl_fb_shim.c"),
                "-lm", "-o", str(exe),
            ]
            subprocess.check_call(cmd)
            subprocess.check_call([str(exe)])


if __name__ == "__main__":
    unittest.main()
```

`tools/tests/test_gl_fb_shim.c`:
```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void gl_fb_shim_test_reset(void);
const uint32_t *gl_fb_shim_test_backbuf(void);
void glViewport(int x, int y, int width, int height);
void glClearColor(float r, float g, float b, float a);
void glClear(uint32_t mask);

int main(void) {
    gl_fb_shim_test_reset();
    glViewport(0, 0, 800, 480);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x4000);
    const uint32_t *bb = gl_fb_shim_test_backbuf();
    assert(bb[0] == 0xff0000ffu); /* RGBA red */
    puts("test_gl_fb_shim: baseline clear OK");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: FAIL (`<linux/fb.h>` missing on macOS or `gl_fb_shim_test_reset` undefined).

- [ ] **Step 3: Add `GL_FB_SHIM_HOST_TEST` guard and test hooks to `tools/arm_gl/gl_fb_shim.c`**

1. Wrap `#include <sys/ioctl.h>`, `#include <sys/mman.h>`, `#include <linux/fb.h>` and the `/dev/fb0` open/mmap block in `eglCreateWindowSurface` with `#ifndef GL_FB_SHIM_HOST_TEST`.
2. At the bottom of `tools/arm_gl/gl_fb_shim.c`, add:
```c
#ifdef GL_FB_SHIM_HOST_TEST
void gl_fb_shim_test_reset(void) {
    for (int i = 1; i < MAX_TEXTURES; i++) free(g_gl.textures[i].pixels);
    for (int i = 1; i < MAX_BUFFERS; i++) free(g_gl.buffers[i].data);
    memset(&g_gl, 0, sizeof(g_gl));
    g_gl.fb_fd = -1;
    g_gl.vp_w = FB_W;
    g_gl.vp_h = FB_H;
    g_gl.sc_w = FB_W;
    g_gl.sc_h = FB_H;
    g_gl.clear_color = 0xff000000u;
    g_gl.clear_color_f[3] = 1.0f;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        g_gl.programs[i].alpha = 1.0f;
        for (int c = 0; c < 4; c++) {
            g_gl.programs[i].skin_color[c] = 1.0f;
            g_gl.programs[i].fg_color[c] = 1.0f;
            g_gl.programs[i].fill_color[c] = 1.0f;
        }
        g_gl.programs[i].mvp[0] = g_gl.programs[i].mvp[5] =
        g_gl.programs[i].mvp[10] = g_gl.programs[i].mvp[15] = 1.0f;
    }
}
const uint32_t *gl_fb_shim_test_backbuf(void) {
    return g_gl.backbuf;
}
#endif
```

- [ ] **Step 4: Run host pixel test harness**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_gl/gl_fb_shim.c tools/tests/test_gl_fb_shim.c tools/tests/test_emu_plan2_gl_fb_shim.py
git commit -m "tools/arm_gl: add host-compiled C pixel test harness for gl_fb_shim"
```

---

### Task 2: Honor `glTexParameteri` Wrap Modes (`GL_REPEAT` `0x2901` vs `GL_CLAMP_TO_EDGE` `0x812F`)

**Files:**
- Modify: `tools/arm_gl/gl_fb_shim.c:30-37,696-730,1040-1186`
- Modify: `tools/tests/test_gl_fb_shim.c`

**Interfaces:**
- Consumes: `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S[0x2802] / GL_TEXTURE_WRAP_T[0x2803], GL_REPEAT[0x2901] / GL_CLAMP_TO_EDGE[0x812F])`.
- Produces: Per-texture `wrap_s`, `wrap_t` fields on `GlTex` (default `0x2901` per OpenGL ES 2.0 spec, or `0x812F` when set) and `wrap_coord(int c, int size, uint32_t mode)` helper used in `draw_quad` (including negative UV safety).

- [ ] **Step 1: Add failing `GL_REPEAT` tiling and negative-UV tests to `tools/tests/test_gl_fb_shim.c`**

Add `test_tex_wrap_repeat()` that uploads a 2×2 checkerboard texture (`[RED, GREEN; BLUE, WHITE]`), sets `GL_TEXTURE_WRAP_S = GL_REPEAT` (`0x2901`) and `GL_TEXTURE_WRAP_T = GL_REPEAT`, draws a 4×4 quad with UVs `[-1.0, 1.0]` (`u` spanning 2 full periods including negative UVs), and asserts pixel `(0,0)` matches `(2,0)` and `(0,2)` (repeating pattern rather than clamped edge color). Also verify `GL_CLAMP_TO_EDGE` (`0x812F`) clamps UVs `> 1.0` to the edge texel.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: FAIL (clamped pixels instead of repeated pattern).

- [ ] **Step 3: Implement `wrap_s`/`wrap_t` in `tools/arm_gl/gl_fb_shim.c`**

1. Add `uint32_t wrap_s, wrap_t;` to `GlTex`. Initialize both to `0x812Fu` (`GL_CLAMP_TO_EDGE`) in `glGenTextures` (matching current default behavior when unspecified, and switching to `0x2901u` whenever IGDT calls `glTexParameteri(..., 0x2802/0x2803, 0x2901)`).
2. In `glTexParameteri`:
```c
void glTexParameteri(uint32_t target, uint32_t pname, int param) {
    (void)target;
    uint32_t id = g_gl.bound_tex[g_gl.active_tex];
    if (id == 0 || id >= MAX_TEXTURES) return;
    if (pname == 0x2802) g_gl.textures[id].wrap_s = (uint32_t)param;
    else if (pname == 0x2803) g_gl.textures[id].wrap_t = (uint32_t)param;
}
```
3. Add helper:
```c
static inline int wrap_coord(int c, int size, uint32_t mode) {
    if (mode == 0x2901u) { /* GL_REPEAT */
        int r = c % size;
        return r < 0 ? r + size : r;
    }
    return c < 0 ? 0 : (c >= size ? size - 1 : c);
}
```
4. In `draw_quad`, disable Fast Path D (`can_direct_x`) whenever `tex->wrap_s == 0x2901u && (sx0 < 0 || sx0 + span_w > tw)`, and use `wrap_coord(v_fp >> 16, th, tex->wrap_t)` and `wrap_coord(u_fp >> 16, tw, tex->wrap_s)` in Path E (and Path D's `sy`).

- [ ] **Step 4: Run host pixel test suite**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_gl/gl_fb_shim.c tools/tests/test_gl_fb_shim.c
git commit -m "tools/arm_gl: implement GL_REPEAT and GL_CLAMP_TO_EDGE texture wrap modes"
```

---

### Task 3: Affine Triangle/Quad Rasterizer Fallback for Rotated & Skewed Quads

**Files:**
- Modify: `tools/arm_gl/gl_fb_shim.c:973-1186`
- Modify: `tools/tests/test_gl_fb_shim.c`

**Interfaces:**
- Consumes: `fetch_vertex(idx, &x, &y, &u, &v)`, `clip_scissor()`, `wrap_coord()`, `blend_pixel()`.
- Produces:
  - `is_axis_aligned_quad(const float x[4], const float y[4])` predicate: keeps existing AABB fast paths when vertices form an axis-aligned rectangle (`<= 2` distinct `x` and `<= 2` distinct `y` within `1e-3f`).
  - `draw_triangle_affine(...)` barycentric/edge-function rasterizer called for the two triangles `(0,1,2)` and `(1,3,2)` (strip) or `(0,1,2)` and `(0,2,3)` when rotated/skewed, handling `PROG_SOLID`, `PROG_FONT`, and `PROG_TEX_SKIN` cleanly and returning safely on zero-area (`fabsf(area) < 1e-4f`) degenerate triangles.

- [ ] **Step 1: Add failing rotated-quad and degenerate-quad tests to `tools/tests/test_gl_fb_shim.c`**

Add `test_rotated_quad_and_degenerate()`:
1. Draw a 40×40 solid red quad rotated 45° around screen center `(400, 240)` using an `MVPMatrix` rotation. Assert the center `(400, 240)` is red, AND a corner of the axis-aligned bounding box outside the diamond (e.g. `(374, 214)`) remains **black** (the old AABB code erroneously fills the entire bounding square!).
2. Draw a textured 90°-rotated quad with distinct colors at each corner and assert the texture rotated 90° rather than remaining upright.
3. Draw a collinear (zero-area) quad and assert no crash and zero pixels modified.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: FAIL (AABB bounding box corner is erroneously filled red).

- [ ] **Step 3: Implement `is_axis_aligned_quad` and `draw_triangle_affine` in `tools/arm_gl/gl_fb_shim.c`**

1. Add `draw_triangle_affine(float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2)` using standard 2D edge functions `e(a,b,p) = (p.x - a.x)*(b.y - a.y) - (p.y - a.y)*(b.x - a.x)`:
   - Compute `float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);`
   - If `fabsf(area) < 1e-4f`, return immediately.
   - If `area < 0`, swap vertices 1 and 2 and negate `area` so winding is positive.
   - Compute clipped bounding box `[bx0, by0, bx1, by1]` via `clip_scissor`, mark backbuffer dirty, iterate `(px + 0.5f, py + 0.5f)`, test `w0 >= 0 && w1 >= 0 && w2 >= 0`, compute `u = (w0*u0 + w1*u1 + w2*u2) * inv_area`, `v = (w0*v0 + w1*v1 + w2*v2) * inv_area`, sample/tint/blend pixel.
2. In `draw_quad(int i0, int i1, int i2, int i3)` (and `glDrawArrays`/`glDrawElements` triangle dispatch):
   - Check if the 4 projected vertices are axis-aligned (`x` values all within `1e-3f` of `minx` or `maxx`, and `y` values all within `1e-3f` of `miny` or `maxy`, and `u` only varies along `x` while `v` only varies along `y`).
   - If NOT axis-aligned (rotated/skewed/UV-rotated), dispatch two calls to `draw_triangle_affine` (`(0,1,2)` and `(1,3,2)` for `GL_TRIANGLE_STRIP`, or pass triangle vertices directly from `GL_TRIANGLES`).

- [ ] **Step 4: Run host pixel test suite**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_gl/gl_fb_shim.c tools/tests/test_gl_fb_shim.c
git commit -m "tools/arm_gl: add affine triangle rasterizer fallback for rotated/skewed quads"
```

---

### Task 4: Honor `glBlendFunc` / `glBlendFuncSeparate` Alpha Blend Factors

**Files:**
- Modify: `tools/arm_gl/gl_fb_shim.c:95-145,453-457,959-975`
- Modify: `tools/tests/test_gl_fb_shim.c`

**Interfaces:**
- Consumes: `glBlendFunc(sfactor, dfactor)` and `glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha)`.
- Produces: Stored blend factor fields (`blend_src_rgb`, `blend_dst_rgb`, `blend_src_a`, `blend_dst_a`, initialized to `GL_ONE, GL_ZERO, GL_ONE, GL_ZERO` or `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) and `blend_pixel()` supporting IGDT's `glBlendFuncSeparate(0x0302, 0x0303, 1, 1)` (`SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE`) with saturating alpha (`oa = (a + ba > 255) ? 255 : (a + ba)`).

- [ ] **Step 1: Add failing `glBlendFuncSeparate` FBO alpha test to `tools/tests/test_gl_fb_shim.c`**

Add `test_blend_func_separate()`:
1. Clear FBO texture to RGBA `(0, 0, 0, 128)`.
2. Call `glEnable(0x0BE2)` and `glBlendFuncSeparate(0x0302, 0x0303, 0x0001, 0x0001)` (`SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE`).
3. Draw a solid quad with `fillColor = (1.0, 0.0, 0.0, 0.5)` (`a = 128`). Assert destination alpha saturates to `255` (`128 + 128`, clamped to `255`), NOT `192` (`128 + 128*127/255`).
4. Draw a second quad with `a = 200` over `ba = 200` and verify saturation at `255` (no 8-bit wrap).

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: FAIL (alpha equals `192` instead of `255`).

- [ ] **Step 3: Implement `glBlendFunc` / `glBlendFuncSeparate` tracking in `tools/arm_gl/gl_fb_shim.c`**

1. Add `uint32_t blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a;` to `g_gl`, defaulted in `init_gl_once()` and `gl_fb_shim_test_reset()` to `0x0302u, 0x0303u, 0x0001u, 0x0303u` (matching existing default before any `glBlendFunc*` call).
2. Store factors in `glBlendFunc` (`src_rgb = src_a = sfactor; dst_rgb = dst_a = dfactor;`) and `glBlendFuncSeparate`.
3. Add a fast `blend_alpha(uint32_t sa, uint32_t da)` helper in `blend_pixel`:
```c
static inline uint32_t blend_alpha_channel(uint32_t a, uint32_t ba) {
    uint32_t oa;
    if (g_gl.blend_src_a == 1u && g_gl.blend_dst_a == 1u) {
        oa = a + ba; /* IGDT FBO: GL_ONE, GL_ONE */
    } else if (g_gl.blend_src_a == 0x0302u && g_gl.blend_dst_a == 0x0303u) {
        oa = div255(a * a + ba * (255u - a));
    } else if (g_gl.blend_src_a == 1u && g_gl.blend_dst_a == 0u) {
        oa = a;
    } else {
        oa = a + div255(ba * (255u - a));
    }
    return oa > 255u ? 255u : oa;
}
```
Use `blend_alpha_channel(a, ba)` inside `blend_pixel()`.

- [ ] **Step 4: Run host pixel test suite**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_gl/gl_fb_shim.c tools/tests/test_gl_fb_shim.c
git commit -m "tools/arm_gl: honor glBlendFuncSeparate alpha blend factors (IGDT FBO ONE,ONE)"
```

---

### Task 5: Implement `PROG_BLUR` Text-Glow Shader (`blur_fs @ 0x8d4bc0`) & Rebuild ARM Prebuilts

**Files:**
- Modify: `tools/arm_gl/gl_fb_shim.c:62-74,607-653,1074-1097`
- Modify: `tools/tests/test_gl_fb_shim.c`
- Modify: `tools/prebuilt/libEGL.so.1`, `tools/prebuilt/libGLESv2.so.2` (via `tools/build_shims.sh`)

**Interfaces:**
- Consumes: Uniforms emitted by IGDT `blur_fs @ 0x8d4bc0`: `textureSize` (`vec2`), `kernel_radius` (`float`), `kernel_data` (`mat4[30]` = `480` floats via `glUniformMatrix4fv`), and `texture` (`sampler2D`).
- Produces:
  - `glGetUniformLocation` returning `7` for `"textureSize"`, `8` for `"kernel_radius"`, `9` for `"kernel_data"` (or `"kernel_data[0]"`).
  - Exact `blur_fs @ 0x8d4bc0` convolution in `PROG_BLUR` (`fontcolor + glowvalue * 1.7f`, clamped to `[0, 255]`, `clampedSample` returning 0 outside `[0, w-1] x [0, h-1]`, `kernel_radius` clamped to `[1, 30]`).
  - Rebuilt ARMv7 NEON prebuilts passing `test_plan4_display_performance.py`.

- [ ] **Step 1: Add failing `PROG_BLUR` glow test (plus out-of-range `kernel_radius` safety test) to `tools/tests/test_gl_fb_shim.c`**

Add `test_prog_blur_glow()`:
1. Create a 16×16 FBO texture with a single white opaque pixel at `(8, 8)` and transparent black elsewhere.
2. Bind a shader program whose source contains `"kernel_data"`, set `textureSize = (16, 16)`, `kernel_radius = 2.0f`, and `kernel_data[0]` with non-zero weights `w(0,0)=0.25, w(1,0)=0.125, w(1,1)=0.0625`.
3. Render full-screen NDC quad into a second 16×16 FBO. Assert adjacent pixel `(7, 8)` now has non-zero glow (`> 0`) instead of remaining `0`, while far corner `(0, 0)` remains `0`.
4. Also set `kernel_radius = 99.0f` and `kernel_radius = 0.0f` and assert clean clamping without out-of-bounds memory access.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v`
Expected: FAIL (neighbor pixel `(7, 8)` is `0` under pass-through).

- [ ] **Step 3: Implement `blur_fs` uniforms and convolution in `tools/arm_gl/gl_fb_shim.c`, then rebuild ARM prebuilts**

1. Add to `GlProg`:
```c
    float tex_size[2];
    float kernel_radius;
    float kernel_data[30 * 16];
```
2. In `glGetUniformLocation`:
```c
    if (strcmp(name, "textureSize") == 0) return 7;
    if (strcmp(name, "kernel_radius") == 0) return 8;
    if (strncmp(name, "kernel_data", 11) == 0) return 9;
```
3. Store `loc == 7` in `glUniform2f`, `loc == 8` in `glUniform1f`, and `loc == 9` in `glUniformMatrix4fv` (`int n = count; if (n > 30) n = 30; if (n > 0 && v) memcpy(p->kernel_data, v, (size_t)n * 16 * sizeof(float));`).
4. In `draw_quad` `PROG_BLUR` branch:
   - If `prog->kernel_radius < 1.5f`, keep the fast pass-through copy.
   - Otherwise, compute `int rad = (int)prog->kernel_radius; if (rad > 30) rad = 30; int fullsize = rad * 2 - 1; int offset = fullsize / 2;`.
   - For each output pixel `(px, py)`, sample `fontcolor = clampedSample(sx, sy)` and sum `weight * clampedSample(sx + lx - offset, sy + ly - offset)` using the exact triangular index `idx = maxv*(maxv+1)/2 + minv` into `prog->kernel_data[idx]` (clamped `< 480`), then write `clamp(fontcolor + glow * 1.7f, 0..255)` per RGBA channel.
5. Rebuild ARMv7 NEON shims:
   Run: `./tools/build_shims.sh --force --update-prebuilt`

- [ ] **Step 4: Run host pixel tests and ARM NEON/softfloat verification tests**

Run: `python3 tools/tests/test_emu_plan2_gl_fb_shim.py -v && python3 tools/tests/test_plan4_display_performance.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_gl/gl_fb_shim.c tools/tests/test_gl_fb_shim.c tools/prebuilt/libEGL.so.1 tools/prebuilt/libGLESv2.so.2
git commit -m "tools/arm_gl: implement PROG_BLUR text-glow shader and refresh NEON prebuilts"
```
