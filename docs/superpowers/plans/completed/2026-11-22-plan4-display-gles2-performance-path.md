# 32-Bit Direct Scanout, NEON GLES2 Rasterizer & Dirty-Frame Notification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the double color conversion (`RGBA8888 -> RGB565 -> XRGB8888`) and redundant per-tick `768 KB` DMA + `memcmp` polling between `tools/arm_gl/gl_fb_shim.c` and `qemu/hw/display/imx6_ipu.c`, and enable ARMv7 Cortex-A9 NEON vectorization in `tools/build_shims.sh`.

**Architecture:** Today, `gl_fb_shim.c` renders the 800x480 UI in 32-bit `RGBA8888` (`g_gl.backbuf`), down-converts every pixel to 16-bit `RGB565` in guest software on `eglSwapBuffers`, and writes it to `/dev/fb0`. Meanwhile, `imx6_ipu.c` DMA-reads `/dev/fb0` on every QEMU display refresh tick, `memcmp`s the entire frame, and up-converts 16-bit `RGB565` back to 32-bit `XRGB8888`. By adding a 32-bit direct-scanout & swap-counter fast path in `imx6_ipu.c` (via spare IPU register offset `0x002000fc` / `IPU_QEMU_SWAP_GENERATION` or 32-bit `BGRA8888` scanout when `eglSwapBuffers` writes the backbuffer), `eglSwapBuffers` in `gl_fb_shim.c` can hand off 32-bit true-color pixels and notify `imx6_ipu.c` only when a new frame is actually swapped. We also enable `-mcpu=cortex-a9 -mfpu=neon -ftree-vectorize` in `tools/build_shims.sh`.

**Tech Stack:** C (`qemu/hw/display/imx6_ipu.c`, `tools/arm_gl/gl_fb_shim.c`), Bash (`tools/build_shims.sh`), Python 3 (`tools/tests/test_plan4_display_performance.py`).

**Spec:** `docs/architecture.md`, `docs/hmi-internals.md`

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- Backward compatibility with raw `/dev/fb0` writes (such as early boot splash or `opt/screenshot` reading `/dev/fb0` in 16bpp `RGB565`) MUST be preserved: `gl_fb_shim.c` should still update `/dev/fb0` so `opt/screenshot` works, while giving `imx6_ipu.c` a zero-loss 32-bit `XRGB8888` scanout buffer and a frame generation counter so `imx6_ipu_gfx_update` skips DMA and `memcmp` completely on idle ticks when `swap_gen` has not changed!
- In `qemu/hw/display/imx6_ipu.c`, `RGBA8888` bytes `[R, G, B, A]` (little-endian `uint32_t`: `(A << 24) | (B << 16) | (G << 8) | R`) must be converted or written as QEMU `PIXMAN_x8r8g8b8` (`0xff000000 | (R << 16) | (G << 8) | B`) so red and blue channels are never swapped.

## Review Focus

- `opt/screenshot` reads `/dev/fb0` directly (`16bpp` `RGB565`). If `gl_fb_shim.c` stopped writing `/dev/fb0`, `opt/screenshot` would capture a blank screen. Instead, if `gl_fb_shim.c` writes its 32-bit `XRGB8888` scanout buffer to the second half of the `/dev/fb0` mapping (`g_gl.fb_mmap + 800 * 480 * 2`, which is inside `finfo.smem_len = 800 * 480 * 4` because `FB_MULTI_BUFFER=2` allocates 2 frames = `1,536,000` bytes, or when `smem_len >= 800 * 480 * 6`) OR `gl_fb_shim.c` packs `g_gl.backbuf` directly in `0xffRRGGBB` (`XRGB8888`) order and passes its physical address / generation counter to `rcd330-vip` / `imx6_ipu`, let's check which is cleaner:
  - Wait: if `g_gl.backbuf` in `gl_fb_shim.c` stores pixels in `(a << 24) | (r << 16) | (g << 8) | b` (`ARGB8888` / `PIXMAN_a8r8g8b8`) internally instead of `ABGR8888`, then:
    1. Converting `ARGB8888` to `RGB565` for `/dev/fb0` is even simpler: `((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f)` (3 shifts + 2 ORs per pixel, or 2 pixels at a time in 32-bit registers!).
    2. Even better: in `qemu/hw/display/imx6_ipu.c`, we can add a 16-bit -> 32-bit lookup table (`uint32_t rgb565_lut[65536]`) initialized once in `imx6_ipu_realize` AND a dirty-page/dirty-memory check (`memory_region_get_dirty` or fast 64-bit word scan) so `imx6_ipu_gfx_update` converts `800x480` RGB565 via a single `dst[x] = s->rgb565_lut[src[x]]` table lookup without any per-pixel multiply/divide (`* 255 / 31` and `* 255 / 63`)!
- Look at lines 250-254 of `qemu/hw/display/imx6_ipu.c` right now:
  ```c
  uint16_t p = src[y * RCD330_WIDTH + x];
  uint8_t r = ((p >> 11) & 0x1f) * 255 / 31;
  uint8_t g = ((p >> 5) & 0x3f) * 255 / 63;
  uint8_t b = (p & 0x1f) * 255 / 31;
  dst[y * dst_stride + x] = 0xff000000u | (r << 16) | (g << 8) | b;
  ```
  Doing three integer divisions (`/ 31`, `/ 63`, `/ 31`) per pixel for `384,000` pixels every frame is `1,152,000` divisions per frame on the host, AND in `gl_fb_shim.c` lines 248-259 we can pack 2 pixels per loop iteration using 32-bit stores (`uint32_t *dst32`) AND compile with Cortex-A9 NEON (`-mcpu=cortex_a9 -mfpu=neon -ftree-vectorize`)!
- Furthermore, we can expose a fast 32-bit scanout register on `imx6_ipu` (or `rcd330-vip` MMIO `0x021fc030`) so when `gl_fb_shim.c` is active, it can also notify `imx6_ipu` of the exact dirty bounding box `(min_y, max_y)` modified during the frame!

---

### Task 1: Precomputed `rgb565_lut[65536]` & Partial Dirty-Row Scanout in `qemu/hw/display/imx6_ipu.c`

**Files:**
- Modify: `qemu/hw/display/imx6_ipu.c:50-64,217-303`
- Test: `tools/tests/test_plan4_display_performance.py`

**Interfaces:**
- Consumes: `IMX6IPUState` in `qemu/hw/display/imx6_ipu.c`.
- Produces:
  - `uint32_t rgb565_lut[65536]` precomputed in `imx6_ipu_realize` (zero integer divisions during frame updates).
  - Row-level dirty tracking (`min_dirty_y`, `max_dirty_y`) in `imx6_ipu_gfx_update` so only modified scanlines are converted and sent to `qemu_console_update(s->con, 0, min_y, RCD330_WIDTH, max_y - min_y + 1)` instead of re-converting and blitting the entire `800x480` surface when only a clock digit, slider, or button highlight changes!

- [ ] **Step 1: Write the failing test in `tools/tests/test_plan4_display_performance.py`**

```python
#!/usr/bin/env python3
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TestPlan4DisplayPerformance(unittest.TestCase):
    def test_imx6_ipu_uses_rgb565_lut_and_dirty_row_rect(self):
        ipu_c = (ROOT / "qemu/hw/display/imx6_ipu.c").read_text()
        self.assertIn("rgb565_lut", ipu_c)
        self.assertIn("min_dirty_y", ipu_c)
        self.assertNotIn("* 255 / 31;", ipu_c.split("imx6_ipu_gfx_update")[1])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan4_display_performance.py -v`
Expected: FAIL (`rgb565_lut` not found in `imx6_ipu.c`).

- [ ] **Step 3: Implement `rgb565_lut[65536]` and dirty-row bounding rect in `qemu/hw/display/imx6_ipu.c`**

In `qemu/hw/display/imx6_ipu.c`:
1. Add `uint32_t *rgb565_lut;` to `struct IMX6IPUState`.
2. In `imx6_ipu_realize`, allocate and populate `s->rgb565_lut = g_new(uint32_t, 65536);`:
```c
    s->rgb565_lut = g_new(uint32_t, 65536);
    for (uint32_t p = 0; p < 65536; p++) {
        uint32_t r = ((p >> 11) & 0x1f) * 255 / 31;
        uint32_t g = ((p >> 5) & 0x3f) * 255 / 63;
        uint32_t b = (p & 0x1f) * 255 / 31;
        s->rgb565_lut[p] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
```
3. In `imx6_ipu_gfx_update`, compare each row `y` (`0..RCD330_HEIGHT-1`) against `s->prev_fb`:
   - Track `int min_dirty_y = RCD330_HEIGHT, max_dirty_y = -1;`.
   - For each row `y`, if `!s->have_prev_fb || memcmp(row_cur, row_prev, row_bytes) != 0`, update `min_dirty_y`/`max_dirty_y`, copy `row_cur` to `row_prev`, and convert only that row using `s->rgb565_lut[src[x]]` (for `bpp == 16`).
   - If `max_dirty_y < 0`, return `false`.
   - Call `qemu_console_update(s->con, 0, min_dirty_y, RCD330_WIDTH, max_dirty_y - min_dirty_y + 1);`.

- [ ] **Step 4: Build QEMU and run the test**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_plan4_display_performance.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/display/imx6_ipu.c
git -C qemu commit -m "hw/display/imx6_ipu: use precomputed RGB565 LUT and dirty-row partial updates"
```

---

### Task 2: Dirty-Row Tracking, Paired 32-Bit RGB565 Scanout & Cortex-A9 NEON Vectorization in `tools/arm_gl/gl_fb_shim.c`

**Files:**
- Modify: `tools/arm_gl/gl_fb_shim.c`
- Modify: `tools/build_shims.sh`
- Modify: `tools/tests/test_plan4_display_performance.py`

**Interfaces:**
- Consumes: `glClear`, `draw_quad`, and `eglSwapBuffers` in `tools/arm_gl/gl_fb_shim.c`.
- Produces:
  - Dirty vertical span tracking (`g_gl.dirty_y0`, `g_gl.dirty_y1`) in `gl_fb_shim.c`: `glClear` and `draw_quad` record the union of modified scanlines `[y0, y1)` when rendering to `g_gl.backbuf`, and `eglSwapBuffers` only converts and writes `[dirty_y0, dirty_y1)` to `/dev/fb0` (using 32-bit paired pixel writes: 2 RGB565 pixels per `uint32_t` store), resetting the dirty range to empty after each swap!
  - Cortex-A9 NEON vectorization flags (`-mcpu=cortex-a9 -mfpu=neon -ftree-vectorize -ffast-math`) in `tools/build_shims.sh`.

- [ ] **Step 1: Add test for dirty-row tracking in `gl_fb_shim.c` and NEON flags in `build_shims.sh`**

Append to `TestPlan4DisplayPerformance` in `tools/tests/test_plan4_display_performance.py`:

```python
    def test_gl_fb_shim_tracks_dirty_rows_and_uses_neon(self):
        gl_c = (ROOT / "tools/arm_gl/gl_fb_shim.c").read_text()
        shims_sh = (ROOT / "tools/build_shims.sh").read_text()
        self.assertIn("dirty_y0", gl_c)
        self.assertIn("dirty_y1", gl_c)
        self.assertIn("mark_backbuf_dirty", gl_c)
        self.assertIn("neon", shims_sh)
        self.assertIn("-ftree-vectorize", shims_sh)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_plan4_display_performance.py -v`
Expected: FAIL (`dirty_y0` not found in `gl_fb_shim.c`).

- [ ] **Step 3: Implement `mark_backbuf_dirty`, paired 32-bit RGB565 writes in `eglSwapBuffers`, and NEON compiler flags**

1. In `tools/arm_gl/gl_fb_shim.c`:
   - Add `int dirty_y0, dirty_y1;` to `g_gl`, initialized in `init_gl_once()` to `g_gl.dirty_y0 = 0; g_gl.dirty_y1 = FB_H;`.
   - Add helper:
     ```c
     static inline void mark_backbuf_dirty(const uint32_t *dst, int y0, int y1) {
         if (dst == g_gl.backbuf) {
             if (y0 < g_gl.dirty_y0) g_gl.dirty_y0 = y0;
             if (y1 > g_gl.dirty_y1) g_gl.dirty_y1 = y1;
         }
     }
     ```
   - Call `mark_backbuf_dirty(dst, y0, y1)` in `glClear` and `draw_quad`.
   - In `eglSwapBuffers`, clamp `[dirty_y0, dirty_y1]` to `[0, FB_H]`; if `dirty_y0 >= dirty_y1`, return `1` immediately without touching `/dev/fb0`. Otherwise convert only rows `y = dirty_y0 .. dirty_y1 - 1` using paired 32-bit stores (2 RGB565 pixels per `uint32_t` write to `g_gl.fb_mmap`), and reset `g_gl.dirty_y0 = FB_H; g_gl.dirty_y1 = 0;`.
2. In `tools/build_shims.sh`:
   - Add `-mcpu=cortex-a9 -mfpu=neon -ftree-vectorize -ffast-math` to the `arm-linux-gnueabi-gcc` and `zig cc` (`-mcpu=cortex_a9+neon -ftree-vectorize -ffast-math`) commands for `gl_fb_shim.c`.
   - Rebuild the shims and update `tools/prebuilt/` (`./tools/build_shims.sh --force --update-prebuilt`) and refresh the rootfs (`./tools/build_rootfs.sh --force`).

- [ ] **Step 4: Run all tests to verify they pass**

Run: `python3 -m unittest discover -s tools/tests -v`
Expected: PASS (all tests across Plans 1, 2, 3, and 4 passing).

- [ ] **Step 5: Commit**

```bash
git add tools/arm_gl/gl_fb_shim.c tools/build_shims.sh tools/prebuilt/ tools/tests/test_plan4_display_performance.py
git commit -m "tools/arm_gl: add dirty-row partial fb0 scanout and Cortex-A9 NEON vectorization"
```
