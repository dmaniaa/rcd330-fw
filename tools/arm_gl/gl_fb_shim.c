/*
 * Lightweight ARMv7 EGL 1.4 + OpenGL ES 2.0 Software Framebuffer Shim
 * for VW RCD 330 opt/hmi (IGDT) on QEMU (/dev/fb0, 800x480).
 *
 * Implements the 11 egl* and 58 gl* symbols used by IGDT in 5624_vw/opt/hmi,
 * rendering textured/tinted quads with MVPMatrix, Alpha, v_skin_color, and
 * alpha blending directly to /dev/fb0.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define FB_W 800
#define FB_H 480

#define MAX_TEXTURES 4096
#define MAX_BUFFERS  512
#define MAX_PROGRAMS 64
#define MAX_SHADERS  128
#define MAX_FBOS     32

typedef struct {
    int used;
    int width;
    int height;
    uint32_t fmt;
    uint32_t *pixels; /* RGBA8888 */
} GlTex;

typedef struct {
    int used;
    size_t size;
    uint8_t *data;
} GlBuf;

typedef struct {
    int used;
    int tex_id;
} GlFbo;

enum {
    PROG_TEX_SKIN = 0,
    PROG_SOLID    = 1,
    PROG_FONT     = 2,
    PROG_BLUR     = 3,
};

typedef struct {
    int used;
    int type;
    int kind;
} GlShader;

typedef struct {
    int used;
    int kind;
    float mvp[16];
    float alpha;
    float skin_color[4];
    float fg_color[4];
    float fill_color[4];
    int tex_unit;
    int pos_loc;
    int tex_loc;
} GlProg;

typedef struct {
    int enabled;
    int size;
    uint32_t type;
    int normalized;
    int stride;
    uintptr_t offset;
    int buf_id;
} AttribState;

static struct {
    int fb_fd;
    uint8_t *fb_mmap;
    size_t fb_size;
    int fb_bpp;
    int fb_stride;
    int fb_swap_rb;

    uint32_t backbuf[FB_W * FB_H]; /* RGBA8888 */
    int dirty_y0, dirty_y1;
    uint32_t clear_color;
    float clear_color_f[4];
    int vp_x, vp_y, vp_w, vp_h;
    int blend_enabled;
    int scissor_enabled;
    int sc_x, sc_y, sc_w, sc_h;

    uint32_t bound_tex[8];
    int active_tex;
    uint32_t bound_vbo;
    uint32_t bound_ibo;
    uint32_t bound_fbo;
    uint32_t cur_prog;

    GlTex textures[MAX_TEXTURES];
    GlBuf buffers[MAX_BUFFERS];
    GlFbo fbos[MAX_FBOS];
    GlShader shaders[MAX_SHADERS];
    GlProg programs[MAX_PROGRAMS];
    AttribState attribs[16];
} g_gl;

static void init_gl_once(void)
{
    static int inited = 0;
    if (inited) return;
    inited = 1;
    memset(&g_gl, 0, sizeof(g_gl));
    g_gl.fb_fd = -1;
    g_gl.fb_swap_rb = 1;
    g_gl.dirty_y0 = 0;
    g_gl.dirty_y1 = FB_H;
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
        g_gl.programs[i].mvp[0] = 1.0f;
        g_gl.programs[i].mvp[5] = 1.0f;
        g_gl.programs[i].mvp[10] = 1.0f;
        g_gl.programs[i].mvp[15] = 1.0f;
    }
}

static inline void mark_backbuf_dirty(const uint32_t *dst, int y0, int y1) {
    if (dst == g_gl.backbuf && y1 > y0) {
        if (y0 < g_gl.dirty_y0) g_gl.dirty_y0 = y0;
        if (y1 > g_gl.dirty_y1) g_gl.dirty_y1 = y1;
    }
}

/* ---------------- Vivante FB helpers ---------------- */
void *fbGetDisplayByIndex(int index) { (void)index; return (void *)(uintptr_t)1; }
void *fbCreateWindow(void *disp, int x, int y, int w, int h) {
    (void)disp; (void)x; (void)y; (void)w; (void)h;
    return (void *)(uintptr_t)1;
}
void fbDestroyWindow(void *win) { (void)win; }
void fbGetWindowGeometry(void *win, int *x, int *y, int *w, int *h) {
    (void)win;
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = FB_W;
    if (h) *h = FB_H;
}

/* ---------------- EGL 1.4 ---------------- */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef int EGLint;

EGLDisplay eglGetDisplay(void *native_display) {
    (void)native_display;
    return (EGLDisplay)(uintptr_t)1;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    (void)dpy;
    init_gl_once();
    if (major) *major = 1;
    if (minor) *minor = 4;
    return 1;
}

EGLBoolean eglBindAPI(EGLenum api) { (void)api; return 1; }

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config) {
    (void)dpy; (void)attrib_list;
    if (configs && config_size > 0) configs[0] = (EGLConfig)(uintptr_t)1;
    if (num_config) *num_config = 1;
    return 1;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint *attrib_list) {
    (void)dpy; (void)config; (void)share_context; (void)attrib_list;
    return (EGLContext)(uintptr_t)1;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  void *win, const EGLint *attrib_list) {
    (void)dpy; (void)config; (void)win; (void)attrib_list;
    init_gl_once();
    if (g_gl.fb_fd < 0) {
        g_gl.fb_fd = open("/dev/fb0", O_RDWR);
        if (g_gl.fb_fd >= 0) {
            struct fb_var_screeninfo vinfo;
            struct fb_fix_screeninfo finfo;
            if (ioctl(g_gl.fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 &&
                ioctl(g_gl.fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0) {
                g_gl.fb_bpp = vinfo.bits_per_pixel;
                g_gl.fb_stride = finfo.line_length;
                g_gl.fb_size = finfo.smem_len;
                /* Internal backbuf is OpenGL RGBA (red@0, blue@16); i.MX6 32bpp fb0 is BGRA/XRGB (red@16, blue@0). */
                g_gl.fb_swap_rb = (vinfo.red.offset > vinfo.blue.offset) ? 1 : 0;
                g_gl.fb_mmap = mmap(NULL, g_gl.fb_size, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, g_gl.fb_fd, 0);
                if (g_gl.fb_mmap == MAP_FAILED) g_gl.fb_mmap = NULL;
                fprintf(stderr, "gl_fb_shim: fb0 bpp=%d stride=%d size=%zu red=%u green=%u blue=%u swap_rb=%d mmap=%p\n",
                        g_gl.fb_bpp, g_gl.fb_stride, g_gl.fb_size,
                        (unsigned)vinfo.red.offset, (unsigned)vinfo.green.offset,
                        (unsigned)vinfo.blue.offset, g_gl.fb_swap_rb, g_gl.fb_mmap);
            }
        } else {
            fprintf(stderr, "gl_fb_shim: failed to open /dev/fb0\n");
        }
    }
    return (EGLSurface)(uintptr_t)1;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    (void)dpy; (void)draw; (void)read; (void)ctx;
    return 1;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) { (void)dpy; (void)ctx; return 1; }
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface sfc) { (void)dpy; (void)sfc; return 1; }
EGLBoolean eglTerminate(EGLDisplay dpy) { (void)dpy; return 1; }
EGLint eglGetError(void) { return 0x3000; }

typedef uint32_t __attribute__((__may_alias__)) u32_alias_t;
typedef uint16_t __attribute__((__may_alias__)) u16_alias_t;

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    (void)dpy; (void)surface;
    static int swap_cnt = 0;
    if (!g_gl.fb_mmap) return 1;

    int y0 = g_gl.dirty_y0;
    int y1 = g_gl.dirty_y1;
    if (y0 < 0) y0 = 0;
    if (y1 > FB_H) y1 = FB_H;
    if (g_gl.fb_stride > 0 && g_gl.fb_size > 0) {
        while (y1 > y0 && (size_t)y1 * (size_t)g_gl.fb_stride > g_gl.fb_size) {
            y1--;
        }
    }
    if (y0 >= y1) {
        g_gl.dirty_y0 = FB_H;
        g_gl.dirty_y1 = 0;
        return 1;
    }

    if (swap_cnt < 3) {
        int nz = 0, rgb_nz = 0;
        for (int i = 0; i < FB_W * FB_H; i++) {
            uint32_t c = g_gl.backbuf[i];
            if (c != 0) nz++;
            if ((c & 0x00ffffffu) != 0) rgb_nz++;
        }
        fprintf(stderr, "gl_fb_shim: eglSwapBuffers #%d mmap=%p backbuf_nz=%d rgb_nz=%d\n", ++swap_cnt, g_gl.fb_mmap, nz, rgb_nz);
    }

    if (g_gl.fb_bpp == 16) {
        for (int y = y0; y < y1; y++) {
            uint8_t *row_ptr = g_gl.fb_mmap + y * g_gl.fb_stride;
            const uint32_t *restrict src = &g_gl.backbuf[y * FB_W];
            if (((uintptr_t)row_ptr & 3u) == 0u) {
                u32_alias_t *restrict dst32 = (u32_alias_t *)row_ptr;
                for (int x = 0; x < FB_W; x += 2) {
                    uint32_t c0 = src[x];
                    uint32_t c1 = src[x + 1];
                    uint32_t p0 = (((c0 >> 3) & 0x1fu) << 11) |
                                  (((c0 >> 10) & 0x3fu) << 5) |
                                  ((c0 >> 19) & 0x1fu);
                    uint32_t p1 = (((c1 >> 3) & 0x1fu) << 11) |
                                  (((c1 >> 10) & 0x3fu) << 5) |
                                  ((c1 >> 19) & 0x1fu);
                    dst32[x >> 1] = p0 | (p1 << 16);
                }
            } else {
                u16_alias_t *restrict dst = (u16_alias_t *)row_ptr;
                for (int x = 0; x < FB_W; x++) {
                    uint32_t c = src[x];
                    uint16_t r = (c >> 3) & 0x1f;
                    uint16_t g = (c >> 10) & 0x3f;
                    uint16_t b = (c >> 19) & 0x1f;
                    dst[x] = (r << 11) | (g << 5) | b;
                }
            }
        }
    } else if (g_gl.fb_bpp == 32) {
        if (g_gl.fb_swap_rb) {
            /* Convert OpenGL RGBA8888 (0xAABBGGRR) -> Linux/i.MX6 XRGB8888/BGRA32 (0xAARRGGBB) */
            for (int y = y0; y < y1; y++) {
                const uint32_t *restrict src = &g_gl.backbuf[y * FB_W];
                u32_alias_t *restrict dst = (u32_alias_t *)(g_gl.fb_mmap + (size_t)y * g_gl.fb_stride);
                for (int x = 0; x < FB_W; x++) {
                    uint32_t c = src[x];
                    dst[x] = (c & 0xff00ff00u) |
                             ((c & 0x00ff0000u) >> 16) |
                             ((c & 0x000000ffu) << 16);
                }
            }
        } else if (g_gl.fb_stride == FB_W * 4) {
            memcpy(g_gl.fb_mmap + (size_t)y0 * FB_W * 4,
                   &g_gl.backbuf[y0 * FB_W],
                   (size_t)(y1 - y0) * FB_W * 4);
        } else {
            for (int y = y0; y < y1; y++) {
                memcpy(g_gl.fb_mmap + y * g_gl.fb_stride,
                       &g_gl.backbuf[y * FB_W], FB_W * 4);
            }
        }
    }
    g_gl.dirty_y0 = FB_H;
    g_gl.dirty_y1 = 0;
    return 1;
}

/* ---------------- OpenGL ES 2.0 ---------------- */
static uint32_t *current_target(int *w, int *h) {
    if (g_gl.bound_fbo > 0 && g_gl.bound_fbo < MAX_FBOS &&
        g_gl.fbos[g_gl.bound_fbo].used) {
        int tid = g_gl.fbos[g_gl.bound_fbo].tex_id;
        if (tid > 0 && tid < MAX_TEXTURES && g_gl.textures[tid].pixels) {
            *w = g_gl.textures[tid].width;
            *h = g_gl.textures[tid].height;
            return g_gl.textures[tid].pixels;
        }
    }
    *w = FB_W;
    *h = FB_H;
    return g_gl.backbuf;
}

/* GL convention: window/FBO row 0 is the BOTTOM (NDC y = -1, v = 0).
 * FBO-backed textures are stored in that GL order so that sampling them
 * with standard UVs (v = 1 at the top) composites upright. Only the
 * default backbuffer is stored top-down, because it is scanned out
 * directly to /dev/fb0. Returns 1 when rows must be flipped. */
static int target_is_top_down(void) {
    int w, h;
    return current_target(&w, &h) == g_gl.backbuf;
}

static inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

void glViewport(int x, int y, int width, int height) {
    g_gl.vp_x = x; g_gl.vp_y = y; g_gl.vp_w = width; g_gl.vp_h = height;
}
void glScissor(int x, int y, int width, int height) {
    g_gl.sc_x = x; g_gl.sc_y = y; g_gl.sc_w = width; g_gl.sc_h = height;
}
static inline uint8_t clamp_u8(float f) {
    if (f <= 0.0f) return 0;
    if (f >= 1.0f) return 255;
    return (uint8_t)(f * 255.0f + 0.5f);
}

static void clip_scissor(int dw, int dh, int *x0, int *y0, int *x1, int *y1) {
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > dw) *x1 = dw;
    if (*y1 > dh) *y1 = dh;
    if (g_gl.scissor_enabled) {
        int sx0 = g_gl.sc_x;
        int sx1 = g_gl.sc_x + g_gl.sc_w;
        int sy0 = g_gl.sc_y;
        int sy1 = g_gl.sc_y + g_gl.sc_h;
        if (target_is_top_down()) {
            sy0 = dh - (g_gl.sc_y + g_gl.sc_h);
            sy1 = dh - g_gl.sc_y;
        }
        if (sx0 > *x0) *x0 = sx0;
        if (sy0 > *y0) *y0 = sy0;
        if (sx1 < *x1) *x1 = sx1;
        if (sy1 < *y1) *y1 = sy1;
    }
}

void glClearColor(float r, float g, float b, float a) {
    g_gl.clear_color_f[0] = r;
    g_gl.clear_color_f[1] = g;
    g_gl.clear_color_f[2] = b;
    g_gl.clear_color_f[3] = a;
    g_gl.clear_color = pack_rgba(clamp_u8(r), clamp_u8(g), clamp_u8(b), clamp_u8(a));
}
void glClear(uint32_t mask) {
    static int logged = 0;
    if (!logged) { logged = 1; fprintf(stderr, "gl_fb_shim: first glClear(0x%x)\n", mask); }
    if (!(mask & 0x4000)) return;
    int w, h;
    uint32_t *dst = current_target(&w, &h);
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    clip_scissor(w, h, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) return;
    mark_backbuf_dirty(dst, y0, y1);
    uint32_t c = g_gl.clear_color;
    if (x0 == 0 && y0 == 0 && x1 == w && y1 == h) {
        size_t n = (size_t)w * (size_t)h;
        if (c == 0) {
            memset(dst, 0, n * 4);
        } else {
            for (size_t i = 0; i < n; i++) dst[i] = c;
        }
    } else {
        int span = x1 - x0;
        for (int py = y0; py < y1; py++) {
            uint32_t *row = &dst[py * w + x0];
            if (c == 0) {
                memset(row, 0, (size_t)span * 4);
            } else {
                for (int i = 0; i < span; i++) row[i] = c;
            }
        }
    }
}
void glEnable(uint32_t cap) {
    if (cap == 0x0be2) g_gl.blend_enabled = 1;
    if (cap == 0x0c11) g_gl.scissor_enabled = 1;
}
void glDisable(uint32_t cap) {
    if (cap == 0x0be2) g_gl.blend_enabled = 0;
    if (cap == 0x0c11) g_gl.scissor_enabled = 0;
}
uint8_t glIsEnabled(uint32_t cap) {
    if (cap == 0x0be2) return (uint8_t)g_gl.blend_enabled;
    if (cap == 0x0c11) return (uint8_t)g_gl.scissor_enabled;
    return 0;
}
void glBlendFunc(uint32_t sfactor, uint32_t dfactor) { (void)sfactor; (void)dfactor; }
void glBlendFuncSeparate(uint32_t sr, uint32_t dr, uint32_t sa, uint32_t da) {
    (void)sr; (void)dr; (void)sa; (void)da;
}
void glDepthMask(uint8_t flag) { (void)flag; }
void glDepthFunc(uint32_t func) { (void)func; }
void glCullFace(uint32_t mode) { (void)mode; }
void glFrontFace(uint32_t mode) { (void)mode; }
void glFlush(void) {}
void glFinish(void) {}
uint32_t glGetError(void) { return 0; }
const char *glGetString(uint32_t name) {
    if (name == 0x1f02) return "OpenGL ES 2.0 RCD330-FB";
    return "RCD330-QEMU";
}
void glGetFloatv(uint32_t pname, float *params) {
    if (!params) return;
    if (pname == 0x0c22) { /* GL_COLOR_CLEAR_VALUE */
        params[0] = g_gl.clear_color_f[0];
        params[1] = g_gl.clear_color_f[1];
        params[2] = g_gl.clear_color_f[2];
        params[3] = g_gl.clear_color_f[3];
    } else {
        *params = 0.0f;
    }
}
void glGetIntegerv(uint32_t pname, int *params) {
    if (!params) return;
    switch (pname) {
    case 0x0d33: /* GL_MAX_TEXTURE_SIZE */
    case 0x84e8: /* GL_MAX_RENDERBUFFER_SIZE */
        *params = 2048;
        break;
    case 0x0ba2: /* GL_VIEWPORT */
        params[0] = g_gl.vp_x;
        params[1] = g_gl.vp_y;
        params[2] = g_gl.vp_w;
        params[3] = g_gl.vp_h;
        break;
    case 0x0c10: /* GL_SCISSOR_BOX */
        params[0] = g_gl.sc_x;
        params[1] = g_gl.sc_y;
        params[2] = g_gl.sc_w;
        params[3] = g_gl.sc_h;
        break;
    case 0x8069: /* GL_TEXTURE_BINDING_2D */
        *params = (int)g_gl.bound_tex[g_gl.active_tex];
        break;
    case 0x8b8d: /* GL_CURRENT_PROGRAM */
        *params = (int)g_gl.cur_prog;
        break;
    case 0x8ca6: /* GL_FRAMEBUFFER_BINDING */
        *params = (int)g_gl.bound_fbo;
        break;
    case 0x8894: /* GL_ARRAY_BUFFER_BINDING */
        *params = (int)g_gl.bound_vbo;
        break;
    case 0x8895: /* GL_ELEMENT_ARRAY_BUFFER_BINDING */
        *params = (int)g_gl.bound_ibo;
        break;
    default:
        *params = 0;
        break;
    }
}

uint32_t glCreateShader(uint32_t type) {
    init_gl_once();
    for (int i = 1; i < MAX_SHADERS; i++) {
        if (!g_gl.shaders[i].used) {
            g_gl.shaders[i].used = 1;
            g_gl.shaders[i].type = type;
            g_gl.shaders[i].kind = PROG_TEX_SKIN;
            return i;
        }
    }
    return 1;
}
void glDeleteShader(uint32_t s) { if (s < MAX_SHADERS) g_gl.shaders[s].used = 0; }
void glShaderSource(uint32_t s, int count, const char **str, const int *len) {
    (void)len;
    if (s == 0 || s >= MAX_SHADERS || !str) return;
    for (int i = 0; i < count; i++) {
        if (!str[i]) continue;
        if (strstr(str[i], "fillColor")) g_gl.shaders[s].kind = PROG_SOLID;
        else if (strstr(str[i], "fgColor")) g_gl.shaders[s].kind = PROG_FONT;
        else if (strstr(str[i], "kernel_data")) g_gl.shaders[s].kind = PROG_BLUR;
    }
}
void glCompileShader(uint32_t s) { (void)s; }
void glGetShaderiv(uint32_t s, uint32_t pname, int *params) {
    (void)s; (void)pname; if (params) *params = 1;
}
void glGetShaderInfoLog(uint32_t s, int max, int *len, char *log) {
    (void)s; (void)max; if (len) *len = 0; if (log) log[0] = 0;
}

uint32_t glCreateProgram(void) {
    init_gl_once();
    for (int i = 1; i < MAX_PROGRAMS; i++) {
        if (!g_gl.programs[i].used) {
            g_gl.programs[i].used = 1;
            g_gl.programs[i].kind = PROG_TEX_SKIN;
            g_gl.programs[i].pos_loc = 0;
            g_gl.programs[i].tex_loc = 1;
            return i;
        }
    }
    return 1;
}
void glDeleteProgram(uint32_t p) { if (p < MAX_PROGRAMS) g_gl.programs[p].used = 0; }
void glAttachShader(uint32_t p, uint32_t s) {
    if (p > 0 && p < MAX_PROGRAMS && s > 0 && s < MAX_SHADERS) {
        if (g_gl.shaders[s].kind != PROG_TEX_SKIN) {
            g_gl.programs[p].kind = g_gl.shaders[s].kind;
        }
    }
}
void glLinkProgram(uint32_t p) { (void)p; }
void glUseProgram(uint32_t p) { g_gl.cur_prog = p; }
void glGetProgramiv(uint32_t p, uint32_t pname, int *params) {
    (void)p; (void)pname; if (params) *params = 1;
}
void glGetProgramInfoLog(uint32_t p, int max, int *len, char *log) {
    (void)p; (void)max; if (len) *len = 0; if (log) log[0] = 0;
}

int glGetAttribLocation(uint32_t p, const char *name) {
    (void)p;
    if (!name) return 0;
    if (strcmp(name, "tpos") == 0 || strstr(name, "tex") || strstr(name, "Tex") || strstr(name, "uv")) return 1;
    if (strstr(name, "col") || strstr(name, "Col")) return 2;
    return 0;
}
void glBindAttribLocation(uint32_t p, uint32_t idx, const char *name) {
    (void)p; (void)idx; (void)name;
}
void glEnableVertexAttribArray(uint32_t idx) {
    if (idx < 16) g_gl.attribs[idx].enabled = 1;
}
void glDisableVertexAttribArray(uint32_t idx) {
    if (idx < 16) g_gl.attribs[idx].enabled = 0;
}
void glVertexAttribPointer(uint32_t idx, int size, uint32_t type,
                           uint8_t norm, int stride, const void *ptr) {
    if (idx >= 16) return;
    g_gl.attribs[idx].size = size;
    g_gl.attribs[idx].type = type;
    g_gl.attribs[idx].normalized = norm;
    g_gl.attribs[idx].stride = stride;
    g_gl.attribs[idx].offset = (uintptr_t)ptr;
    g_gl.attribs[idx].buf_id = g_gl.bound_vbo;
}

/* Uniforms: 1=MVPMatrix, 2=s_texture/texture, 3=Alpha, 4=v_skin_color, 5=fgColor, 6=fillColor */
int glGetUniformLocation(uint32_t p, const char *name) {
    (void)p;
    if (!name) return -1;
    if (strcmp(name, "MVPMatrix") == 0) return 1;
    if (strcmp(name, "s_texture") == 0 || strcmp(name, "texture") == 0) return 2;
    if (strcmp(name, "Alpha") == 0) return 3;
    if (strcmp(name, "v_skin_color") == 0) return 4;
    if (strcmp(name, "fgColor") == 0) return 5;
    if (strcmp(name, "fillColor") == 0) return 6;
    return -1;
}

void glUniform1i(int loc, int v0) {
    GlProg *p = &g_gl.programs[g_gl.cur_prog < MAX_PROGRAMS ? g_gl.cur_prog : 0];
    if (loc == 2) p->tex_unit = v0;
}
void glUniform1f(int loc, float v0) {
    GlProg *p = &g_gl.programs[g_gl.cur_prog < MAX_PROGRAMS ? g_gl.cur_prog : 0];
    if (loc == 3) p->alpha = v0;
}
void glUniform2f(int loc, float v0, float v1) {
    (void)loc; (void)v0; (void)v1;
}
void glUniform4f(int loc, float x, float y, float z, float w) {
    GlProg *p = &g_gl.programs[g_gl.cur_prog < MAX_PROGRAMS ? g_gl.cur_prog : 0];
    if (loc == 4) {
        p->skin_color[0] = x; p->skin_color[1] = y;
        p->skin_color[2] = z; p->skin_color[3] = w;
    } else if (loc == 5) {
        p->fg_color[0] = x; p->fg_color[1] = y;
        p->fg_color[2] = z; p->fg_color[3] = w;
    } else if (loc == 6) {
        p->fill_color[0] = x; p->fill_color[1] = y;
        p->fill_color[2] = z; p->fill_color[3] = w;
    }
}
void glUniform4fv(int loc, int count, const float *v) {
    (void)count;
    if (v) glUniform4f(loc, v[0], v[1], v[2], v[3]);
}
void glUniformMatrix4fv(int loc, int count, uint8_t trans, const float *v) {
    (void)count; (void)trans;
    GlProg *p = &g_gl.programs[g_gl.cur_prog < MAX_PROGRAMS ? g_gl.cur_prog : 0];
    if (loc == 1 && v) memcpy(p->mvp, v, 16 * sizeof(float));
}

void glGenBuffers(int n, uint32_t *bufs) {
    init_gl_once();
    for (int i = 0; i < n; i++) {
        bufs[i] = 0;
        for (int j = 1; j < MAX_BUFFERS; j++) {
            if (!g_gl.buffers[j].used) {
                g_gl.buffers[j].used = 1;
                bufs[i] = j;
                break;
            }
        }
    }
}
void glDeleteBuffers(int n, const uint32_t *bufs) {
    for (int i = 0; i < n; i++) {
        uint32_t id = bufs[i];
        if (id > 0 && id < MAX_BUFFERS) {
            free(g_gl.buffers[id].data);
            memset(&g_gl.buffers[id], 0, sizeof(GlBuf));
        }
    }
}
void glBindBuffer(uint32_t target, uint32_t buf) {
    if (target == 0x8892) g_gl.bound_vbo = buf;
    else if (target == 0x8893) g_gl.bound_ibo = buf;
}
void glBufferData(uint32_t target, ptrdiff_t size, const void *data, uint32_t usage) {
    (void)usage;
    uint32_t id = (target == 0x8893) ? g_gl.bound_ibo : g_gl.bound_vbo;
    if (id == 0 || id >= MAX_BUFFERS || size <= 0) return;
    g_gl.buffers[id].data = realloc(g_gl.buffers[id].data, (size_t)size);
    g_gl.buffers[id].size = (size_t)size;
    if (data) memcpy(g_gl.buffers[id].data, data, (size_t)size);
}
void glBufferSubData(uint32_t target, ptrdiff_t off, ptrdiff_t size, const void *data) {
    uint32_t id = (target == 0x8893) ? g_gl.bound_ibo : g_gl.bound_vbo;
    if (id == 0 || id >= MAX_BUFFERS || !data || !g_gl.buffers[id].data) return;
    if ((size_t)(off + size) <= g_gl.buffers[id].size) {
        memcpy(g_gl.buffers[id].data + off, data, (size_t)size);
    }
}

void glGenTextures(int n, uint32_t *tex) {
    init_gl_once();
    for (int i = 0; i < n; i++) {
        tex[i] = 0;
        for (int j = 1; j < MAX_TEXTURES; j++) {
            if (!g_gl.textures[j].used) {
                g_gl.textures[j].used = 1;
                tex[i] = j;
                break;
            }
        }
    }
}
void glDeleteTextures(int n, const uint32_t *tex) {
    for (int i = 0; i < n; i++) {
        uint32_t id = tex[i];
        if (id > 0 && id < MAX_TEXTURES) {
            free(g_gl.textures[id].pixels);
            memset(&g_gl.textures[id], 0, sizeof(GlTex));
        }
    }
}
void glActiveTexture(uint32_t tex) {
    int u = (int)(tex - 0x84c0);
    if (u >= 0 && u < 8) g_gl.active_tex = u;
}
void glBindTexture(uint32_t target, uint32_t tex) {
    (void)target;
    g_gl.bound_tex[g_gl.active_tex] = tex;
}
void glTexParameteri(uint32_t target, uint32_t pname, int param) {
    (void)target; (void)pname; (void)param;
}
void glPixelStorei(uint32_t pname, int param) { (void)pname; (void)param; }

static void convert_row(uint32_t *dst, const uint8_t *src, int count,
                        uint32_t fmt, uint32_t type) {
    if (fmt == 0x1908 && type == 0x1401) { /* GL_RGBA + GL_UNSIGNED_BYTE */
        memcpy(dst, src, (size_t)count * 4);
        return;
    }
    if (type == 0x8363) { /* GL_UNSIGNED_SHORT_5_6_5 */
        const uint16_t *s = (const uint16_t *)src;
        for (int i = 0; i < count; i++) {
            uint16_t v = s[i];
            uint32_t r = (v >> 11) & 0x1f;
            uint32_t g = (v >> 5) & 0x3f;
            uint32_t b = v & 0x1f;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            dst[i] = 0xff000000u | (b << 16) | (g << 8) | r;
        }
        return;
    }
    if (type == 0x8033) { /* GL_UNSIGNED_SHORT_4_4_4_4 */
        const uint16_t *s = (const uint16_t *)src;
        for (int i = 0; i < count; i++) {
            uint16_t v = s[i];
            uint32_t r = ((v >> 12) & 0xf) * 17u;
            uint32_t g = ((v >> 8) & 0xf) * 17u;
            uint32_t b = ((v >> 4) & 0xf) * 17u;
            uint32_t a = (v & 0xf) * 17u;
            dst[i] = (a << 24) | (b << 16) | (g << 8) | r;
        }
        return;
    }
    if (type == 0x8034) { /* GL_UNSIGNED_SHORT_5_5_5_1 */
        const uint16_t *s = (const uint16_t *)src;
        for (int i = 0; i < count; i++) {
            uint16_t v = s[i];
            uint32_t r = (v >> 11) & 0x1f;
            uint32_t g = (v >> 6) & 0x1f;
            uint32_t b = (v >> 1) & 0x1f;
            uint32_t a = (v & 1) ? 255u : 0u;
            r = (r << 3) | (r >> 2);
            g = (g << 3) | (g >> 2);
            b = (b << 3) | (b >> 2);
            dst[i] = (a << 24) | (b << 16) | (g << 8) | r;
        }
        return;
    }
    if (fmt == 0x1908) { /* GL_RGBA */
        memcpy(dst, src, (size_t)count * 4);
        return;
    }
    if (fmt == 0x1907) { /* GL_RGB */
        for (int i = 0; i < count; i++) {
            const uint8_t *p = src + i * 3;
            dst[i] = 0xff000000u | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        }
        return;
    }
    if (fmt == 0x1906) { /* GL_ALPHA */
        for (int i = 0; i < count; i++) {
            dst[i] = ((uint32_t)src[i] << 24) | 0x00ffffffu;
        }
        return;
    }
    if (fmt == 0x1909) { /* GL_LUMINANCE */
        for (int i = 0; i < count; i++) {
            uint32_t l = src[i];
            dst[i] = 0xff000000u | (l << 16) | (l << 8) | l;
        }
        return;
    }
    if (fmt == 0x190a) { /* GL_LUMINANCE_ALPHA */
        for (int i = 0; i < count; i++) {
            uint32_t l = src[i * 2];
            uint32_t a = src[i * 2 + 1];
            dst[i] = (a << 24) | (l << 16) | (l << 8) | l;
        }
        return;
    }
    for (int i = 0; i < count; i++) dst[i] = 0xffffffffu;
}

static int pixel_bytes(uint32_t fmt, uint32_t type) {
    if (type == 0x8363 || type == 0x8033 || type == 0x8034) return 2;
    if (fmt == 0x1908) return 4;
    if (fmt == 0x1907) return 3;
    if (fmt == 0x190a) return 2;
    return 1;
}

void glTexImage2D(uint32_t target, int level, int intfmt, int w, int h,
                  int border, uint32_t fmt, uint32_t type, const void *data) {
    (void)target; (void)intfmt; (void)border;
    if (level != 0 || w <= 0 || h <= 0) return;
    uint32_t id = g_gl.bound_tex[g_gl.active_tex];
    if (id == 0 || id >= MAX_TEXTURES) return;
    GlTex *t = &g_gl.textures[id];
    t->width = w;
    t->height = h;
    t->fmt = fmt;
    t->pixels = realloc(t->pixels, (size_t)w * h * 4);
    if (!data) {
        memset(t->pixels, 0, (size_t)w * h * 4);
        return;
    }
    convert_row(t->pixels, (const uint8_t *)data, w * h, fmt, type);
}

void glTexSubImage2D(uint32_t target, int level, int xoff, int yoff,
                     int w, int h, uint32_t fmt, uint32_t type, const void *data) {
    (void)target;
    if (level != 0 || !data || w <= 0 || h <= 0) return;
    uint32_t id = g_gl.bound_tex[g_gl.active_tex];
    if (id == 0 || id >= MAX_TEXTURES) return;
    GlTex *t = &g_gl.textures[id];
    if (!t->pixels) return;
    int pb = pixel_bytes(fmt, type);
    const uint8_t *src = (const uint8_t *)data;
    int sx0 = (xoff < 0) ? -xoff : 0;
    int copy_w = w - sx0;
    if (xoff + sx0 + copy_w > t->width) copy_w = t->width - (xoff + sx0);
    if (copy_w <= 0) return;
    for (int y = 0; y < h; y++) {
        int dy = yoff + y;
        if (dy < 0 || dy >= t->height) continue;
        convert_row(&t->pixels[dy * t->width + xoff + sx0],
                    src + (y * w + sx0) * pb, copy_w, fmt, type);
    }
}

void glGenFramebuffers(int n, uint32_t *fbos) {
    init_gl_once();
    for (int i = 0; i < n; i++) {
        fbos[i] = 0;
        for (int j = 1; j < MAX_FBOS; j++) {
            if (!g_gl.fbos[j].used) {
                g_gl.fbos[j].used = 1;
                fbos[i] = j;
                break;
            }
        }
    }
}
void glDeleteFramebuffers(int n, const uint32_t *fbos) {
    for (int i = 0; i < n; i++) {
        if (fbos[i] > 0 && fbos[i] < MAX_FBOS) g_gl.fbos[fbos[i]].used = 0;
    }
}
void glBindFramebuffer(uint32_t target, uint32_t fbo) {
    (void)target; g_gl.bound_fbo = fbo;
}
void glFramebufferTexture2D(uint32_t target, uint32_t att, uint32_t textarget,
                            uint32_t tex, int level) {
    (void)target; (void)att; (void)textarget; (void)level;
    if (g_gl.bound_fbo > 0 && g_gl.bound_fbo < MAX_FBOS) {
        g_gl.fbos[g_gl.bound_fbo].tex_id = tex;
    }
}
uint32_t glCheckFramebufferStatus(uint32_t target) { (void)target; return 0x8cd5; }
void glGenRenderbuffers(int n, uint32_t *rb) {
    for (int i = 0; i < n; i++) rb[i] = (uint32_t)(i + 1);
}
void glDeleteRenderbuffers(int n, const uint32_t *rb) { (void)n; (void)rb; }
void glBindRenderbuffer(uint32_t target, uint32_t rb) { (void)target; (void)rb; }
void glRenderbufferStorage(uint32_t target, uint32_t fmt, int w, int h) {
    (void)target; (void)fmt; (void)w; (void)h;
}
void glFramebufferRenderbuffer(uint32_t target, uint32_t att, uint32_t rt, uint32_t rb) {
    (void)target; (void)att; (void)rt; (void)rb;
}

static void fetch_vertex(int idx, float *x, float *y, float *u, float *v) {
    AttribState *pa = &g_gl.attribs[0];
    AttribState *ta = &g_gl.attribs[1];

    const uint8_t *pbase = (const uint8_t *)pa->offset;
    if (pa->buf_id > 0 && pa->buf_id < MAX_BUFFERS && g_gl.buffers[pa->buf_id].data) {
        pbase = g_gl.buffers[pa->buf_id].data + pa->offset;
    }
    int pstride = pa->stride ? pa->stride : pa->size * 4;
    const float *pf = (const float *)(pbase + idx * pstride);

    float px = pf[0], py = pf[1], pz = (pa->size >= 3) ? pf[2] : 0.0f;
    GlProg *prog = &g_gl.programs[g_gl.cur_prog < MAX_PROGRAMS ? g_gl.cur_prog : 0];
    float cx, cy;
    if (prog->kind == PROG_BLUR) {
        /* prog5_VS: gl_Position = a_position (already in NDC) */
        cx = px;
        cy = py;
    } else {
        const float *m = prog->mvp;
        cx = m[0] * px + m[4] * py + m[8] * pz + m[12];
        cy = m[1] * px + m[5] * py + m[9] * pz + m[13];
        float cw = m[3] * px + m[7] * py + m[11] * pz + m[15];
        if (cw != 0.0f && cw != 1.0f) { cx /= cw; cy /= cw; }
    }

    *x = g_gl.vp_x + (cx + 1.0f) * 0.5f * g_gl.vp_w;
    /* GL window y (row 0 = bottom); flip only for the top-down backbuffer */
    float gl_y = g_gl.vp_y + (cy + 1.0f) * 0.5f * g_gl.vp_h;
    int tw_, th_;
    uint32_t *tgt = current_target(&tw_, &th_);
    *y = (tgt == g_gl.backbuf) ? (float)th_ - gl_y : gl_y;

    if (ta->enabled && prog->kind != PROG_SOLID) {
        const uint8_t *tbase = (const uint8_t *)ta->offset;
        if (ta->buf_id > 0 && ta->buf_id < MAX_BUFFERS && g_gl.buffers[ta->buf_id].data) {
            tbase = g_gl.buffers[ta->buf_id].data + ta->offset;
        }
        int tstride = ta->stride ? ta->stride : ta->size * 4;
        const float *tf = (const float *)(tbase + idx * tstride);
        *u = tf[0];
        *v = tf[1];
    } else {
        *u = 0.0f; *v = 0.0f;
    }
}

static inline uint32_t div255(uint32_t x) {
    return (x + 1u + (x >> 8)) >> 8;
}

static inline uint32_t f_to_256(float f) {
    if (f <= 0.0f) return 0u;
    if (f >= 1.0f) return 256u;
    return (uint32_t)(f * 256.0f + 0.5f);
}

static inline uint32_t blend_pixel(uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t bg) {
    uint32_t inv = 255u - a;
    uint32_t br = bg & 0xffu;
    uint32_t bgc = (bg >> 8) & 0xffu;
    uint32_t bb = (bg >> 16) & 0xffu;
    uint32_t ba = bg >> 24;
    r = div255(r * a + br * inv);
    g = div255(g * a + bgc * inv);
    b = div255(b * a + bb * inv);
    uint32_t oa = a + div255(ba * inv);
    if (oa > 255u) oa = 255u;
    return (oa << 24) | (b << 16) | (g << 8) | r;
}

static void draw_quad(int i0, int i1, int i2, int i3) {
    float x[4], y[4], u[4], v[4];
    int idxs[4] = { i0, i1, i2, i3 };
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    float u_left = 0.0f, u_right = 0.0f, v_top = 0.0f, v_bot = 0.0f;
    for (int k = 0; k < 4; k++) {
        fetch_vertex(idxs[k], &x[k], &y[k], &u[k], &v[k]);
        if (x[k] < minx) { minx = x[k]; u_left = u[k]; }
        if (x[k] > maxx) { maxx = x[k]; u_right = u[k]; }
        if (y[k] < miny) { miny = y[k]; v_top = v[k]; }
        if (y[k] > maxy) { maxy = y[k]; v_bot = v[k]; }
    }

    int dw, dh;
    uint32_t *dst = current_target(&dw, &dh);
    int x0 = (int)minx, y0 = (int)miny, x1 = (int)(maxx + 0.5f), y1 = (int)(maxy + 0.5f);
    clip_scissor(dw, dh, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) return;

    float fw = maxx - minx;
    float fh = maxy - miny;
    if (fw < 1e-4f || fh < 1e-4f) return;

    GlProg *prog = &g_gl.programs[g_gl.cur_prog < MAX_PROGRAMS ? g_gl.cur_prog : 0];
    int blend = g_gl.blend_enabled;
    int span_w = x1 - x0;

    /* Fast path A: PROG_SOLID (fillColor) — never sample bound_tex */
    if (prog->kind == PROG_SOLID) {
        uint32_t r = clamp_u8(prog->fill_color[0]);
        uint32_t g = clamp_u8(prog->fill_color[1]);
        uint32_t b = clamp_u8(prog->fill_color[2]);
        uint32_t a = clamp_u8(prog->fill_color[3]);
        if (blend && a == 0u) return;
        mark_backbuf_dirty(dst, y0, y1);
        uint32_t solid = (a << 24) | (b << 16) | (g << 8) | r;
        if (!blend || a >= 255u) {
            for (int py = y0; py < y1; py++) {
                uint32_t *drow = &dst[py * dw + x0];
                for (int i = 0; i < span_w; i++) drow[i] = solid;
            }
        } else {
            for (int py = y0; py < y1; py++) {
                uint32_t *drow = &dst[py * dw + x0];
                for (int i = 0; i < span_w; i++) {
                    drow[i] = blend_pixel(r, g, b, a, drow[i]);
                }
            }
        }
        return;
    }

    uint32_t tid = g_gl.bound_tex[prog->tex_unit < 8 ? prog->tex_unit : 0];
    GlTex *tex = (tid > 0 && tid < MAX_TEXTURES && g_gl.textures[tid].pixels) ?
                 &g_gl.textures[tid] : NULL;
    if (!tex) return;

    int tw = tex->width;
    int th = tex->height;
    float inv_fw = 1.0f / fw;
    float inv_fh = 1.0f / fh;
    float u_scale = (u_right - u_left) * (float)tw * inv_fw;
    float v_scale = (v_bot - v_top) * (float)th * inv_fh;
    int32_t du_fp = (int32_t)(u_scale * 65536.0f);
    int32_t dv_fp = (int32_t)(v_scale * 65536.0f);
    int32_t u0_fp = (int32_t)((u_left * (float)tw + ((float)x0 + 0.5f - minx) * u_scale) * 65536.0f);
    int32_t v_fp  = (int32_t)((v_top * (float)th + ((float)y0 + 0.5f - miny) * v_scale) * 65536.0f);
    int32_t max_u_fp = (tw - 1) << 16;

    /* Fast path B: PROG_FONT (fgColor.rgb, tex.a * Alpha) */
    if (prog->kind == PROG_FONT) {
        uint32_t fr = clamp_u8(prog->fg_color[0]);
        uint32_t fg = clamp_u8(prog->fg_color[1]);
        uint32_t fb = clamp_u8(prog->fg_color[2]);
        uint32_t sa = f_to_256(prog->alpha);
        if (blend && sa == 0u) return;
        mark_backbuf_dirty(dst, y0, y1);
        for (int py = y0; py < y1; py++, v_fp += dv_fp) {
            int sy = v_fp >> 16;
            if (sy < 0) sy = 0; else if (sy >= th) sy = th - 1;
            const uint32_t *srow = &tex->pixels[sy * tw];
            uint32_t *drow = &dst[py * dw + x0];
            int32_t u_fp = u0_fp;
            for (int i = 0; i < span_w; i++, u_fp += du_fp) {
                int32_t cu = u_fp < 0 ? 0 : (u_fp > max_u_fp ? max_u_fp : u_fp);
                uint32_t a = ((srow[cu >> 16] >> 24) * sa) >> 8;
                if (blend) {
                    if (a == 0u) continue;
                    if (a >= 255u) {
                        drow[i] = 0xff000000u | (fb << 16) | (fg << 8) | fr;
                    } else {
                        drow[i] = blend_pixel(fr, fg, fb, a, drow[i]);
                    }
                } else {
                    drow[i] = (a << 24) | (fb << 16) | (fg << 8) | fr;
                }
            }
        }
        return;
    }

    /* Fast path C: PROG_BLUR (clamped pass-through into FBO, out-of-range -> 0) */
    if (prog->kind == PROG_BLUR) {
        mark_backbuf_dirty(dst, y0, y1);
        int32_t lim_u_fp = tw << 16;
        for (int py = y0; py < y1; py++, v_fp += dv_fp) {
            int sy = v_fp >> 16;
            uint32_t *drow = &dst[py * dw + x0];
            if (sy < 0 || sy >= th) {
                if (!blend) memset(drow, 0, (size_t)span_w * 4);
                continue;
            }
            const uint32_t *srow = &tex->pixels[sy * tw];
            int32_t u_fp = u0_fp;
            for (int i = 0; i < span_w; i++, u_fp += du_fp) {
                if (u_fp < 0 || u_fp >= lim_u_fp) {
                    if (!blend) drow[i] = 0;
                } else {
                    drow[i] = srow[u_fp >> 16];
                }
            }
        }
        return;
    }

    /* PROG_TEX_SKIN: rgb = tex.rgb * v_skin_color.rgb, a = tex.a * Alpha */
    uint32_t sr = f_to_256(prog->skin_color[0]);
    uint32_t sg = f_to_256(prog->skin_color[1]);
    uint32_t sb = f_to_256(prog->skin_color[2]);
    uint32_t sa = f_to_256(prog->alpha);
    if (blend && sa == 0u) return;
    mark_backbuf_dirty(dst, y0, y1);

    int identity_tint = (sr == 256u && sg == 256u && sb == 256u && sa == 256u);
    int is_1to1_x = (du_fp >= 65520 && du_fp <= 65552);
    int sx0 = u0_fp >> 16;
    int can_direct_x = is_1to1_x && sx0 >= 0 && (sx0 + span_w) <= tw;

    /* Fast path D: 1:1 unscaled, untinted blit (covers full-screen FBO & UI sprites) */
    if (identity_tint && can_direct_x) {
        if (!blend) {
            for (int py = y0; py < y1; py++, v_fp += dv_fp) {
                int sy = v_fp >> 16;
                if (sy < 0) sy = 0; else if (sy >= th) sy = th - 1;
                memcpy(&dst[py * dw + x0], &tex->pixels[sy * tw + sx0], (size_t)span_w * 4);
            }
        } else {
            for (int py = y0; py < y1; py++, v_fp += dv_fp) {
                int sy = v_fp >> 16;
                if (sy < 0) sy = 0; else if (sy >= th) sy = th - 1;
                const uint32_t *srow = &tex->pixels[sy * tw + sx0];
                uint32_t *drow = &dst[py * dw + x0];
                for (int i = 0; i < span_w; i++) {
                    uint32_t tc = srow[i];
                    uint32_t a = tc >> 24;
                    if (a == 0u) continue;
                    if (a == 255u) {
                        drow[i] = tc;
                    } else {
                        drow[i] = blend_pixel(tc & 0xffu, (tc >> 8) & 0xffu,
                                              (tc >> 16) & 0xffu, a, drow[i]);
                    }
                }
            }
        }
        return;
    }

    /* General path E: pure integer fixed-point UV + integer tint/blend */
    for (int py = y0; py < y1; py++, v_fp += dv_fp) {
        int sy = v_fp >> 16;
        if (sy < 0) sy = 0; else if (sy >= th) sy = th - 1;
        const uint32_t *srow = &tex->pixels[sy * tw];
        uint32_t *drow = &dst[py * dw + x0];
        int32_t u_fp = u0_fp;

        if (identity_tint) {
            if (!blend) {
                for (int i = 0; i < span_w; i++, u_fp += du_fp) {
                    int32_t cu = u_fp < 0 ? 0 : (u_fp > max_u_fp ? max_u_fp : u_fp);
                    drow[i] = srow[cu >> 16];
                }
            } else {
                for (int i = 0; i < span_w; i++, u_fp += du_fp) {
                    int32_t cu = u_fp < 0 ? 0 : (u_fp > max_u_fp ? max_u_fp : u_fp);
                    uint32_t tc = srow[cu >> 16];
                    uint32_t a = tc >> 24;
                    if (a == 0u) continue;
                    if (a == 255u) {
                        drow[i] = tc;
                    } else {
                        drow[i] = blend_pixel(tc & 0xffu, (tc >> 8) & 0xffu,
                                              (tc >> 16) & 0xffu, a, drow[i]);
                    }
                }
            }
        } else {
            for (int i = 0; i < span_w; i++, u_fp += du_fp) {
                int32_t cu = u_fp < 0 ? 0 : (u_fp > max_u_fp ? max_u_fp : u_fp);
                uint32_t tc = srow[cu >> 16];
                uint32_t a = ((tc >> 24) * sa) >> 8;
                if (blend && a == 0u) continue;
                uint32_t r = ((tc & 0xffu) * sr) >> 8;
                uint32_t g = (((tc >> 8) & 0xffu) * sg) >> 8;
                uint32_t b = (((tc >> 16) & 0xffu) * sb) >> 8;
                if (blend && a < 255u) {
                    drow[i] = blend_pixel(r, g, b, a, drow[i]);
                } else {
                    drow[i] = (a << 24) | (b << 16) | (g << 8) | r;
                }
            }
        }
    }
}

void glDrawArrays(uint32_t mode, int first, int count) {
    static int logged = 0;
    if (!logged) { logged = 1; fprintf(stderr, "gl_fb_shim: first glDrawArrays(mode=%u, first=%d, count=%d)\n", mode, first, count); }
    if (mode == 5 && count >= 4) { /* GL_TRIANGLE_STRIP */
        for (int i = 0; i + 3 < count; i += 2) {
            draw_quad(first + i, first + i + 1, first + i + 2, first + i + 3);
        }
    } else if (mode == 4 && count >= 6) { /* GL_TRIANGLES */
        for (int i = 0; i + 5 < count; i += 6) {
            draw_quad(first + i, first + i + 1, first + i + 2, first + i + 4);
        }
    }
}

static int fetch_index(const void *indices, uint32_t type, int k) {
    const uint8_t *base = (const uint8_t *)indices;
    if (g_gl.bound_ibo > 0 && g_gl.bound_ibo < MAX_BUFFERS && g_gl.buffers[g_gl.bound_ibo].data) {
        base = g_gl.buffers[g_gl.bound_ibo].data + (uintptr_t)indices;
    }
    if (!base) return k;
    if (type == 0x1403) return ((const uint16_t *)base)[k]; /* GL_UNSIGNED_SHORT */
    if (type == 0x1401) return base[k];                     /* GL_UNSIGNED_BYTE */
    return k;
}

void glDrawElements(uint32_t mode, int count, uint32_t type, const void *indices) {
    if (mode == 5 && count >= 4) {
        for (int i = 0; i + 3 < count; i += 2) {
            draw_quad(fetch_index(indices, type, i),
                      fetch_index(indices, type, i + 1),
                      fetch_index(indices, type, i + 2),
                      fetch_index(indices, type, i + 3));
        }
    } else if (mode == 4 && count >= 6) {
        for (int i = 0; i + 5 < count; i += 6) {
            /* Collect the 4 distinct vertex indices of the 2-triangle quad */
            int v0 = fetch_index(indices, type, i);
            int v1 = fetch_index(indices, type, i + 1);
            int v2 = fetch_index(indices, type, i + 2);
            int v3 = fetch_index(indices, type, i + 3);
            if (v3 == v0 || v3 == v1 || v3 == v2) v3 = fetch_index(indices, type, i + 4);
            if (v3 == v0 || v3 == v1 || v3 == v2) v3 = fetch_index(indices, type, i + 5);
            draw_quad(v0, v1, v2, v3);
        }
    }
}
