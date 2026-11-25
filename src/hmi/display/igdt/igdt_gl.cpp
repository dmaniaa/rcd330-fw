// File name is ours; stock name unknown.
#include "display/igdt/igdt_gl.h"

#include <stdio.h>
#include <map>
#include <vector>

#include "control_logic/subsystem/power/vpowerproxy.h"
#include "display/igdt/igdt_ovg.h"
#include "service/osal/osal_api.h"

// DEVIATION: the toolchain has no Khronos/Vivante headers; these are the
// exact prototypes libEGL.so.1 / libGLESv2.so.2 export (all arguments are
// ints, floats or pointers, so the ABI is unambiguous).
extern "C" {
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef void* EGLNativeDisplayType;
typedef void* EGLNativeWindowType;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef int EGLint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef int GLsizeiptr;   // ptrdiff_t on 32-bit ARM

EGLNativeDisplayType fbGetDisplayByIndex(int index);
EGLNativeWindowType fbCreateWindow(EGLNativeDisplayType display, int x, int y, int w, int h);
void fbDestroyWindow(EGLNativeWindowType window);
EGLDisplay eglGetDisplay(EGLNativeDisplayType display);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attribs, EGLConfig* configs, EGLint size,
                           EGLint* count);
EGLBoolean eglBindAPI(EGLenum api);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win,
                                  const EGLint* attribs);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share, const EGLint* attribs);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglTerminate(EGLDisplay dpy);

void glActiveTexture(GLenum texture);
void glAttachShader(GLuint program, GLuint shader);
void glBindAttribLocation(GLuint program, GLuint index, const char* name);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glBindTexture(GLenum target, GLuint texture);
void glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
GLenum glCheckFramebufferStatus(GLenum target);
void glClear(unsigned int mask);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glCompileShader(GLuint shader);
GLuint glCreateProgram();
GLuint glCreateShader(GLenum type);
void glDeleteBuffers(GLsizei n, const GLuint* buffers);
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
void glDeleteProgram(GLuint program);
void glDeleteShader(GLuint shader);
void glDeleteTextures(GLsizei n, const GLuint* textures);
void glDisable(GLenum cap);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glEnable(GLenum cap);
void glEnableVertexAttribArray(GLuint index);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void glGenBuffers(GLsizei n, GLuint* buffers);
void glGenFramebuffers(GLsizei n, GLuint* framebuffers);
void glGenTextures(GLsizei n, GLuint* textures);
GLint glGetAttribLocation(GLuint program, const char* name);
void glGetIntegerv(GLenum pname, GLint* params);
void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
GLint glGetUniformLocation(GLuint program, const char* name);
void glLinkProgram(GLuint program);
void glPixelStorei(GLenum pname, GLint param);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glShaderSource(GLuint shader, GLsizei count, const char* const* source, const GLint* length);
void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei w, GLsizei h, GLint border,
                  GLenum format, GLenum type, const void* pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1i(GLint location, GLint v0);
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void glUniform4fv(GLint location, GLsizei count, const GLfloat* value);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
void glUseProgram(GLuint program);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                           const void* pointer);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
}

namespace {

// GL / EGL enum values used below (Khronos numbering).
const GLenum GL_TEXTURE_2D = 0x0de1;
const GLenum GL_TEXTURE0 = 0x84c0;
const GLenum GL_BLEND = 0x0be2;
const GLenum GL_SCISSOR_TEST = 0x0c11;
const unsigned int GL_COLOR_BUFFER_BIT = 0x4000;
const GLenum GL_SRC_ALPHA = 0x0302;
const GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
const GLenum GL_ONE = 1;
const GLenum GL_FRAMEBUFFER = 0x8d40;
const GLenum GL_COLOR_ATTACHMENT0 = 0x8ce0;
const GLenum GL_ARRAY_BUFFER = 0x8892;
const GLenum GL_DYNAMIC_DRAW = 0x88e4;
const GLenum GL_FLOAT = 0x1406;
const GLenum GL_TRIANGLE_STRIP = 5;
const GLenum GL_RGBA = 0x1908;
const GLenum GL_ALPHA = 0x1906;
const GLenum GL_UNSIGNED_BYTE = 0x1401;
const GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
const GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
const GLenum GL_TEXTURE_WRAP_S = 0x2802;
const GLenum GL_TEXTURE_WRAP_T = 0x2803;
const GLint GL_LINEAR = 0x2601;
const GLint GL_CLAMP_TO_EDGE = 0x812f;
const GLenum GL_UNPACK_ALIGNMENT = 0x0cf5;
const GLenum GL_PACK_ALIGNMENT = 0x0d05;
const GLenum GL_MAX_RENDERBUFFER_SIZE = 0x84e8;
const GLenum GL_FRAGMENT_SHADER = 0x8b30;
const GLenum GL_VERTEX_SHADER = 0x8b31;
const GLenum GL_COMPILE_STATUS = 0x8b81;
const GLenum GL_LINK_STATUS = 0x8b82;
const EGLenum EGL_OPENGL_ES_API = 0x30a0;

// Stock's "no FBO" marker: DAT_00e74d60 / DAT_00e52d94 start as 0x501 and
// are overwritten by glGenFramebuffers / glGenTextures.
const GLuint kNoFbo = 0x501;

// 0x008d4988 eglChooseConfig attributes: RGB 888, any alpha, 16-bit depth,
// any stencil, window + pbuffer surfaces (EGL_SURFACE_TYPE 4), ES2.
const EGLint kConfigAttribs[] = {
    0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, -1, 0x3025, 16, 0x3026, -1,
    0x3033, 4, 0x3040, 1, 0x3038};
// 0x0096f170 eglCreateContext attributes: EGL_CONTEXT_CLIENT_VERSION 2.
const EGLint kContextAttribs[] = {0x3098, 2, 0x3038, 0};

// Stock GLSL, verbatim from .rodata (pointer table 0x008d49cc..0x008d49ec).
// 0x008d59c4 (pointer at 0x008d49cc)
const char kFillFs[] =
    "uniform highp  vec4  fillColor;void main (void){  gl_FragColor = fillColor;}";

// 0x008d5948 (pointer at 0x008d49d0)
const char kFillVs[] =
    "attribute highp vec4    inVertex;uniform mediump mat4    MVPMatrix;void main(void){   gl_P"
    "osition = MVPMatrix * inVertex;}";

// 0x008d5710 (pointer at 0x008d49d4)
const char kSkinFs[] =
    "precision mediump float;varying vec2      v_texCoord;uniform sampler2D s_texture;uniform h"
    "ighp  float Alpha;uniform mediump vec4 v_skin_color;\n"
    "void main(void){vec4  fragColor;fragColor = texture2D(s_texture, v_texCoord);gl_FragColor "
    "= vec4(fragColor.r*v_skin_color.r,fragColor.g*v_skin_color.g,fragColor.b*v_skin_color.b,fr"
    "agColor.a * Alpha);}";

// 0x008d5868 (pointer at 0x008d49d8)
const char kSkinVs[] =
    "attribute vec4 a_position;attribute vec4 tpos;varying   vec2 v_texCoord;uniform mediump ma"
    "t4 MVPMatrix;uniform mediump vec4 v_skin_color;\n"
    "void main(void){   gl_Position = MVPMatrix * a_position;   v_texCoord  = tpos.st;}";

// 0x008d557c (pointer at 0x008d49dc)
const char kSkinFsAlt[] =
    "precision mediump float;varying vec2      v_texCoord;uniform sampler2D s_texture;uniform h"
    "ighp  float Alpha;uniform mediump vec4 v_skin_color;\n"
    "void main(void){gl_FragColor = texture2D(s_texture, v_texCoord);gl_FragColor.r = (gl_FragC"
    "olor.r * v_skin_color.r);gl_FragColor.g = (gl_FragColor.g * v_skin_color.g);gl_FragColor.b"
    " = (gl_FragColor.b * v_skin_color.b);gl_FragColor.a = (gl_FragColor.a * Alpha);}";

// 0x008d536c (pointer at 0x008d49e0)
const char kFontFs[] =
    "precision mediump float;varying vec2      v_texCoord;uniform highp  vec4  fgColor;uniform "
    "sampler2D s_texture;uniform highp  float Alpha;void main(void){//gl_FragColor = fgColor * "
    "texture2D(s_texture, v_texCoord).a;\n"
    "gl_FragColor = fgColor;\n"
    "gl_FragColor.a = texture2D(s_texture, v_texCoord).a;\n"
    "gl_FragColor.a = (gl_FragColor.a * Alpha);}";

// 0x008d54c0 (pointer at 0x008d49e4)
const char kFontVs[] =
    "attribute vec4 a_position;attribute vec4 tpos;varying   vec2 v_texCoord;uniform mediump ma"
    "t4 MVPMatrix;void main(void){   gl_Position = MVPMatrix * a_position;   v_texCoord  = tpos"
    ".st;}";

// 0x008d4bc0 (pointer at 0x008d49e8)
const char kBlurFs[] =
    "precision mediump float;uniform sampler2D texture;varying highp vec2 v_texCoord;uniform ve"
    "c2 textureSize;uniform mat4 kernel_data[30];uniform float kernel_radius;vec4 clampedSample"
    "(vec2 coord){\tif(any(lessThan(coord, vec2(0.0))))\t{\t\treturn vec4(0.0);\t}\telse if(any(great"
    "erThan(coord, vec2(1.0))))\t{\t\treturn vec4(0.0);\t}\telse\t{\t\treturn texture2D(texture, coord)"
    ";\t}}void main(){    highp float xv = 1.0/textureSize.x;    highp float yv = 1.0/textureSiz"
    "e.y;    vec4 fontcolor = clampedSample(v_texCoord);    highp vec4 glowvalue = vec4(0.0);  "
    "  float intensity = 1.7;    {        float loopx =0.0;        float loopy =0.0;        flo"
    "at fullsize = kernel_radius*2.0-1.0;        float offset = float(int(fullsize/2.0));      "
    "  highp float maxlength = length(vec2(fullsize*xv,fullsize*yv));        float left_top_siz"
    "e = kernel_radius;        for(loopx = 0.0; loopx < fullsize; loopx+=1.0)        {         "
    "   for(loopy = 0.0; loopy < fullsize; loopy+=1.0)            {                float x = (l"
    "eft_top_size-1.0)-abs(loopx-(left_top_size-1.0));                float y = (left_top_size-"
    "1.0)-abs(loopy-(left_top_size-1.0));                highp vec2 samplecoord = vec2((loopx-o"
    "ffset)*xv, (loopy-offset)*yv);                float maxv = max(x,y);                float "
    "minv = min(x,y);                int index = int( (maxv * (maxv+1.0) / 2.0) + minv );\t\t\t\tin"
    "t matrix_index = index/16;\t\t\t\tint element_row_index = int(mod(float(index),16.0)/4.0);\t\t\t\t"
    "int element_column_index = int(mod(mod(float(index),16.0),4.0));\t\t\t\tmat4 matrix;\t\t\t\tmatrix"
    " = kernel_data[matrix_index];\t\t\t\tfloat weight = 1.0;\t\t\t\tweight = matrix[element_row_index]"
    "[element_column_index];               glowvalue = glowvalue + weight * clampedSample(v_tex"
    "Coord+samplecoord);            }        }    }\tgl_FragColor = vec4(fontcolor + glowvalue *"
    " intensity );}";

// 0x008d52d8 (pointer at 0x008d49ec)
const char kBlurVs[] =
    "attribute vec4 a_position;attribute vec2 a_texCoord;varying vec2 v_texCoord;void main(){  "
    " gl_Position = a_position;   v_texCoord = a_texCoord;}";

struct GlState {
    EGLNativeDisplayType fb_display;   // 0x00df2f88
    EGLNativeWindowType fb_window;     // 0x00df2f84
    EGLDisplay display;                // 0x00df2f74
    EGLConfig config;                  // 0x00df2f78
    EGLSurface surface;                // 0x00df2f7c
    EGLContext context;                // 0x00df2f80
    GLuint fbo;                        // 0x00e74d60
    GLuint fbo_tex;                    // 0x00e52d94
    GLuint prog_fill;                  // 0x00df2f98
    GLuint prog_skin;                  // 0x00df2f9c
    GLuint prog_skin_alt;              // 0x00df2fa0
    GLuint prog_font;                  // 0x00df2fa4
    GLuint prog_blur;                  // 0x00df2fa8
    GLuint vbo;                        // 0x00df2f94
    char drawn;                        // 0x00df2fac (3 after a draw)
    int w, h;
    // 0x00df2f64..0x00df2f70 last glScissor box (x, y-from-top, w, h).
    int sc_x, sc_y, sc_w, sc_h;
};

GlState g_gl = {0, 0, 0, 0, 0, 0, kNoFbo, kNoFbo, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// 0x0096f10c base 3x3 transform (column-major): pixels -> NDC with y down.
// [0] = 2/w and [4] = -(2/h) are set by IgdtGlInit.
float g_base[9] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f};
// 0x0096f130 MVPMatrix (4x4, column-major).
float g_mvp[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f};
// 0x0096f17c FBO copy quad: 4 x (x, y, z, u, v); x/y set to w/h by IgdtGlInit.
float g_copy_quad[20] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                         0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f};
// 0x0096f1d0 element quad: 4 x (x, y, z, u, v).
float g_quad[20] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};

IgdtDrawState g_state;

// DEVIATION: stock keeps three 100-slot texture caches keyed by path/w/h
// with an LRU countdown (0x00325d30, decremented in 0x00326b3c); ours keeps
// one slot per tex_table index of each skin table for the process lifetime.
struct TexSlot {
    GLuint name;
    GLuint program;
    bool tried;
};
typedef std::map<const IgdtSkinTables*, std::vector<TexSlot> > TexCache;
TexCache g_textures;

const char* const kTexRoot = "/opt";   // tex paths are "gresfiles/..."

// Returns 0 (shader deleted) when compilation fails.
GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        log_simple("IGDT", "IGDT: shader compile failed (type 0x%x)\n", type);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Returns 0 (program deleted) when linking fails; the shaders stay with the caller.
GLuint LinkProgram(GLuint fs, GLuint vs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, fs);
    glAttachShader(program, vs);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        log_simple("IGDT", "IGDT: program link failed\n");
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// One fragment + vertex shader program; both shaders are deleted afterwards.
GLuint BuildProgram(const char* fs_source, const char* vs_source) {
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_source);
    if (!fs) return 0;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_source);
    if (!vs) {
        glDeleteShader(fs);
        return 0;
    }
    GLuint program = LinkProgram(fs, vs);
    glDeleteShader(fs);
    glDeleteShader(vs);
    return program;
}

// Programs in stock order; the skin vertex shader is shared by prog_skin and
// prog_skin_alt.
// 0x00326084 BuildPrograms (shader part of IgdtGlInit)
bool BuildPrograms() {
    g_gl.prog_fill = BuildProgram(kFillFs, kFillVs);
    if (!g_gl.prog_fill) return false;

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kSkinFs);
    if (!fs) return false;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kSkinVs);
    if (!vs) {
        glDeleteShader(fs);
        return false;
    }
    g_gl.prog_skin = LinkProgram(fs, vs);
    glDeleteShader(fs);
    // STOCK-QUIRK: prog_skin's link status is not checked before building
    // prog_skin_alt with the same vertex shader.
    fs = CompileShader(GL_FRAGMENT_SHADER, kSkinFsAlt);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    g_gl.prog_skin_alt = LinkProgram(fs, vs);
    glDeleteShader(fs);
    glDeleteShader(vs);
    if (!g_gl.prog_skin_alt) return false;

    g_gl.prog_font = BuildProgram(kFontFs, kFontVs);
    if (!g_gl.prog_font) return false;
    g_gl.prog_blur = BuildProgram(kBlurFs, kBlurVs);
    return g_gl.prog_blur != 0;
}

// m = m * translate(tx, ty) for a column-major 3x3.
// 0x003251ec Translate
void Translate(float* m, float tx, float ty) {
    m[6] = m[6] + (tx * m[0] + ty * m[3]);
    m[7] = m[7] + (tx * m[1] + ty * m[4]);
    m[8] = m[8] + (tx * m[2] + ty * m[5]);
}

// 3x3 into the 4x4 MVPMatrix layout stock writes (z row/column identity).
void StoreMvp(const float* m) {
    g_mvp[0] = m[0];
    g_mvp[1] = m[1];
    g_mvp[3] = m[2];
    g_mvp[4] = m[3];
    g_mvp[5] = m[4];
    g_mvp[7] = m[5];
    g_mvp[12] = m[6];
    g_mvp[13] = m[7];
    g_mvp[15] = m[8];
}

// MVP = base * translate(origin) * state matrix * translate(-pivot).
// 0x003252b0 SetMvpFromState
void SetMvpFromState(const IgdtDrawState& s) {
    float l[9];
    for (int i = 0; i < 9; ++i) l[i] = g_base[i];
    Translate(l, (float)s.x, (float)s.y);
    const float* t = s.matrix;
    float r[9];
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            r[col * 3 + row] = l[row] * t[col * 3] + l[3 + row] * t[col * 3 + 1] + l[6 + row] * t[col * 3 + 2];
        }
    }
    Translate(r, -s.pivot_x, -s.pivot_y);
    StoreMvp(r);
}

// glScissor to the state's clip (GL y from the bottom), skipped when the box
// did not change.
// 0x003256a0 SetScissorFromState
void SetScissorFromState(const IgdtDrawState& s) {
    int x = s.clip_x0, y = s.clip_y0;
    int w = s.clip_x1 - x, h = s.clip_y1 - y;
    if (x == g_gl.sc_x && y == g_gl.sc_y && w == g_gl.sc_w && h == g_gl.sc_h) return;
    glScissor(x, (int)s.screen_h - y - h, w, h);
    g_gl.sc_x = x;
    g_gl.sc_y = y;
    g_gl.sc_w = w;
    g_gl.sc_h = h;
}

// Colour channel (shift 16 = red, 8 = green, 0 = blue) as 0..1.
float Channel(uint32_t color, int shift) {
    return (float)((color >> shift) & 0xff) / 255.0f;
}

// Alpha byte of color scaled by the state alpha unless that is 0xff.
float ColorAlpha(uint32_t color, uint8_t state_alpha) {
    float a = (float)(color >> 24) / 255.0f;
    if (state_alpha != 0xff) a = a * (float)state_alpha / 255.0f;
    return a;
}

// w x h box at the current MVP in color with the fill program; the
// sequence 0x0032749c and the text background of 0x003278e0 both issue.
// 0x0032749c FillBox (shared tail, also in 0x003278e0)
void FillBox(float w, float h, uint32_t color, uint8_t state_alpha) {
    const float box[12] = {0.0f, 0.0f, 0.0f, w, 0.0f, 0.0f, 0.0f, h, 0.0f, w, h, 0.0f};
    glUseProgram(g_gl.prog_fill);
    glBufferData(GL_ARRAY_BUFFER, sizeof(box), box, GL_DYNAMIC_DRAW);
    glUniformMatrix4fv(glGetUniformLocation(g_gl.prog_fill, "MVPMatrix"), 1, 0, g_mvp);
    glUniform4f(glGetUniformLocation(g_gl.prog_fill, "fillColor"), Channel(color, 16), Channel(color, 8),
                Channel(color, 0), ColorAlpha(color, state_alpha));
    glBindAttribLocation(g_gl.prog_fill, 0, "inVertex");
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// The textured-quad tail shared by 0x00326ee8 and 0x00326b3c.
// 0x00326ee8 DrawTexturedQuad (tail, also inlined in 0x00326b3c)
void DrawTexturedQuad(GLuint program, const float* quad, float alpha, const float* skin) {
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "MVPMatrix"), 1, 0, g_mvp);
    glUniform1i(glGetUniformLocation(program, "s_texture"), 0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_quad), quad, GL_DYNAMIC_DRAW);
    GLint pos = glGetAttribLocation(program, "a_position");
    glEnableVertexAttribArray(pos);
    glVertexAttribPointer(pos, 3, GL_FLOAT, 0, 0x14, 0);
    GLint uv = glGetAttribLocation(program, "tpos");
    glEnableVertexAttribArray(uv);
    glVertexAttribPointer(uv, 2, GL_FLOAT, 0, 0x14, (const void*)0xc);
    glUniform1f(glGetUniformLocation(program, "Alpha"), alpha);
    glUniform4fv(glGetUniformLocation(program, "v_skin_color"), 1, skin);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_gl.drawn = 3;
}

// Program, pixel store, upload and sampling as stock.
// 0x00325728 UploadTexture (GL part of IGDT_LoadOvgTextureEntry)
TexSlot UploadTexture(const IgdtTexEntry& e) {
    TexSlot slot = {0, 0, true};
    glActiveTexture(GL_TEXTURE0);
    GLuint program = g_gl.prog_skin;
    switch (e.fmt) {
    case IGDT_FMT_RGBA8888_B:
        program = g_gl.prog_skin_alt;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        break;
    case IGDT_FMT_RGB565:
        program = g_gl.prog_skin_alt;
        // STOCK-QUIRK: GL_PACK_ALIGNMENT, not UNPACK, for 2-byte formats.
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        break;
    case IGDT_FMT_A8:
    case IGDT_FMT_RGBA8888:
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        break;
    case IGDT_FMT_RGBA5551:
    case IGDT_FMT_RGBA4444:
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        break;
    default:
        return slot;
    }
    std::vector<uint8_t> pixels;
    unsigned format = 0, type = 0;
    if (!IgdtLoadOvgRaw(e, kTexRoot, pixels, &format, &type)) {
        return slot;   // the loader logged the path
    }
    if (pixels.empty()) {
        // DEVIATION: a 0x0 slot would upload from a NULL buffer on stock.
        log_simple("IGDT", "IGDT: empty texture %s\n", e.path);
        return slot;
    }
    slot.program = program;
    glGenTextures(1, &slot.name);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, slot.name);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, e.w, e.h, 0, format, type, &pixels[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 0x00326ee8 passes GL_CLAMP_TO_EDGE (0x00327698, tiled images, GL_REPEAT).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return slot;
}

} // namespace

// 0x0031d6fc IgdtCurrentState
IgdtDrawState& IgdtCurrentState() {
    return g_state;
}

// 0x00326084 IgdtGlInit
bool IgdtGlInit(int w, int h) {
    g_gl.w = w;
    g_gl.h = h;
    g_gl.fb_display = fbGetDisplayByIndex(0);
    g_gl.fb_window = fbCreateWindow(g_gl.fb_display, 0, 0, w, h);
    if (!g_gl.fb_display) return false;
    g_gl.display = eglGetDisplay(g_gl.fb_display);
    if (!g_gl.display) {
        g_gl.display = eglGetDisplay(0);
    }
    if (!eglInitialize(g_gl.display, 0, 0)) {
        puts("EGL init failed!");
        return false;
    }
    EGLint count = 0;
    if (!eglChooseConfig(g_gl.display, kConfigAttribs, &g_gl.config, 1, &count)) {
        puts("EGL Choose Config failed");
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    g_gl.surface = eglCreateWindowSurface(g_gl.display, g_gl.config, g_gl.fb_window, 0);
    g_gl.context = eglCreateContext(g_gl.display, g_gl.config, 0, kContextAttribs);
    if (!eglMakeCurrent(g_gl.display, g_gl.surface, g_gl.surface, g_gl.context)) {
        puts("EGL Make current failed");
    }
    g_gl.fbo = kNoFbo;
    g_gl.fbo_tex = kNoFbo;
    g_copy_quad[5] = (float)(unsigned)w;
    g_copy_quad[11] = (float)(unsigned)h;
    g_copy_quad[15] = g_copy_quad[5];
    g_copy_quad[16] = g_copy_quad[11];
    GLint max_rb = 0;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_rb);
    if (max_rb >= w && max_rb >= h) {
        glGenFramebuffers(1, &g_gl.fbo);
        glGenTextures(1, &g_gl.fbo_tex);
        glBindTexture(GL_TEXTURE_2D, g_gl.fbo_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    if (!BuildPrograms()) return false;
    g_gl.drawn = 0;
    glGenBuffers(1, &g_gl.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.vbo);
    g_base[0] = 2.0f / (float)w;
    g_base[4] = -(2.0f / (float)h);
    glViewport(0, 0, w, h);
    return true;
}

// DEVIATION: no stock counterpart (stock never releases GL state).
void IgdtGlTerm() {
    for (TexCache::iterator it = g_textures.begin(); it != g_textures.end(); ++it) {
        for (size_t i = 0; i < it->second.size(); ++i) {
            if (it->second[i].name) glDeleteTextures(1, &it->second[i].name);
        }
    }
    g_textures.clear();
    if (g_gl.vbo) glDeleteBuffers(1, &g_gl.vbo);
    GLuint* programs[] = {&g_gl.prog_fill, &g_gl.prog_skin, &g_gl.prog_skin_alt, &g_gl.prog_font,
                          &g_gl.prog_blur};
    for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); ++i) {
        if (*programs[i]) glDeleteProgram(*programs[i]);
        *programs[i] = 0;
    }
    if (g_gl.fbo != kNoFbo) glDeleteFramebuffers(1, &g_gl.fbo);
    if (g_gl.fbo_tex != kNoFbo) glDeleteTextures(1, &g_gl.fbo_tex);
    g_gl.vbo = 0;
    g_gl.fbo = kNoFbo;
    g_gl.fbo_tex = kNoFbo;
    if (g_gl.display) {
        eglMakeCurrent(g_gl.display, 0, 0, 0);
        if (g_gl.context) eglDestroyContext(g_gl.display, g_gl.context);
        if (g_gl.surface) eglDestroySurface(g_gl.display, g_gl.surface);
        eglTerminate(g_gl.display);
    }
    if (g_gl.fb_window) fbDestroyWindow(g_gl.fb_window);
    g_gl.display = 0;
    g_gl.context = 0;
    g_gl.surface = 0;
    g_gl.fb_window = 0;
}

// 0x00325d30 IgdtTexture (cache lookup + 0x00325728 load)
GLuint IgdtTexture(const IgdtSkinTables& t, int tex_index, GLuint* program) {
    if (program) *program = 0;
    if (tex_index < 0 || tex_index >= t.tex_count) return 0;
    std::vector<TexSlot>& slots = g_textures[&t];
    if (slots.empty()) {
        TexSlot empty = {0, 0, false};
        slots.assign(t.tex_count, empty);
    }
    TexSlot& slot = slots[tex_index];
    if (program) *program = slot.program;
    if (!slot.tried) {
        slot = UploadTexture(t.textures[tex_index]);   // leaves it bound
        if (program) *program = slot.program;
        return slot.name;
    }
    if (slot.name) {
        // 0x00325d30 cache hit: bind as stock does
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, slot.name);
    }
    return slot.name;
}

// 0x00326a50 IgdtBeginFrame
void IgdtBeginFrame() {
    const IgdtDrawState& s = g_state;
    if (g_gl.fbo != kNoFbo && g_gl.fbo_tex != kNoFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_gl.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_gl.fbo_tex, 0);
        glCheckFramebufferStatus(GL_FRAMEBUFFER);
    }
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, s.screen_w, s.screen_h);
    g_gl.sc_x = 0;
    g_gl.sc_y = 0;
    g_gl.sc_w = s.screen_w;
    g_gl.sc_h = s.screen_h;
}

// Name is ours; no stock counterpart (see IgdtSceneDrawLayers).
void IgdtClearFrame() {
    glScissor(0, 0, g_state.screen_w, g_state.screen_h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// 0x00326ee8 IgdtDrawImage
void IgdtDrawImage(const IgdtSkinTables& t, const IgdtElement& e) {
    const IgdtDrawState& s = g_state;
    SetScissorFromState(s);
    SetMvpFromState(s);
    if (s.flags != 0) return;
    GLuint program = 0;
    if (!IgdtTexture(t, e.tex, &program)) return;   // 0x00325d30 / 0x00325728, leaves it bound
    g_quad[0] = 0.0f;
    g_quad[1] = 0.0f;
    g_quad[5] = (float)(unsigned)e.w;
    g_quad[6] = 0.0f;
    g_quad[8] = 1.0f;
    g_quad[10] = 0.0f;
    g_quad[11] = (float)(unsigned)e.h;
    g_quad[14] = 1.0f;
    g_quad[15] = g_quad[5];
    g_quad[16] = g_quad[11];
    g_quad[18] = 1.0f;
    g_quad[19] = 1.0f;
    float skin[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (s.tint != 0xffffffffu) {
        skin[0] = (float)((s.tint >> 16) & 0xff) / 255.0f;
        skin[1] = (float)((s.tint >> 8) & 0xff) / 255.0f;
        skin[2] = (float)(s.tint & 0xff) / 255.0f;
    }
    DrawTexturedQuad(program, g_quad, (float)s.alpha / 255.0f, skin);
}

// 0x003278e0 IgdtDrawTextCoverage (GL part)
void IgdtDrawTextCoverage(const IgdtTextCoverage& c) {
    const IgdtDrawState& s = g_state;
    const float fg_a = ColorAlpha(s.fg_color, s.alpha);
    SetScissorFromState(s);
    SetMvpFromState(s);
    if ((s.text_align & 0x0c) == 0) {
        FillBox((float)c.fill_w, (float)(int)c.fill_h, s.bg_color, s.alpha);
    }
    g_quad[8] = 1.0f;
    g_quad[14] = 1.0f;
    g_quad[18] = 1.0f;
    g_quad[19] = 1.0f;
    const GLuint program = g_gl.prog_font;
    glUseProgram(program);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glUniformMatrix4fv(glGetUniformLocation(program, "MVPMatrix"), 1, 0, g_mvp);
    glUniform4f(glGetUniformLocation(program, "fgColor"), Channel(s.fg_color, 16), Channel(s.fg_color, 8),
                Channel(s.fg_color, 0), fg_a);
    glUniform1i(glGetUniformLocation(program, "s_texture"), 0);
    GLint uv = glGetAttribLocation(program, "tpos");
    glEnableVertexAttribArray(uv);
    glVertexAttribPointer(uv, 2, GL_FLOAT, 0, 0x14, (const void*)0xc);
    glUniform1f(glGetUniformLocation(program, "Alpha"), (float)s.alpha / 255.0f);
    // 0x00325c84: each 0x01 escape sets fgColor from its two parameter wchars.
    // STOCK-QUIRK: stock issues these while rasterizing the whole line and then
    // draws one quad, so the last escape colours the entire line; the escape's
    // alpha byte ignores the state alpha (the font shader replaces fgColor.a
    // with texture alpha * Alpha anyway).
    for (size_t i = 0; i < c.fg_escape_count; ++i) {
        const uint32_t rg = c.fg_escapes[2 * i], ba = c.fg_escapes[2 * i + 1];
        glUniform4f(glGetUniformLocation(program, "fgColor"), Channel(rg, 8), Channel(rg, 0), Channel(ba, 8),
                    Channel(ba, 0));
    }
    // 0x00325728 with the line as an in-memory fmt 0x0b (A8) entry.
    GLuint tex = 0;
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &tex);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL_ALPHA, c.w, c.h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, c.alpha);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_quad[0] = 0.0f;
    g_quad[1] = 0.0f;
    g_quad[5] = (float)c.w;
    g_quad[6] = 0.0f;
    g_quad[10] = 0.0f;
    g_quad[11] = (float)c.h;
    g_quad[15] = g_quad[5];
    g_quad[16] = g_quad[11];
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_quad), g_quad, GL_DYNAMIC_DRAW);
    GLint pos = glGetAttribLocation(program, "a_position");
    glEnableVertexAttribArray(pos);
    glVertexAttribPointer(pos, 3, GL_FLOAT, 0, 0x14, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_gl.drawn = 3;
    glDeleteTextures(1, &tex);
}

// 0x0032749c IgdtDrawRect
void IgdtDrawRect(uint16_t w, uint16_t h, uint32_t color) {
    const IgdtDrawState& s = g_state;
    if (s.flags != 0) return;
    SetScissorFromState(s);
    if (w == s.screen_w && h == s.screen_h) {
        // 0x00327408: a screen-sized rect clears (within the scissor) instead,
        // with the colour's own alpha.
        glClearColor(Channel(color, 16), Channel(color, 8), Channel(color, 0), (float)(color >> 24) / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_gl.drawn = 3;
        return;
    }
    SetMvpFromState(s);
    FillBox((float)w, (float)h, color, s.alpha);
    g_gl.drawn = 3;
}

// Stock DAT_0096f1cc: set until the first eglSwapBuffers. Written on the
// igdtTask render thread, read by VControlTask; a byte flag as in stock.
static volatile bool g_first_frame_pending = true;

// 0x00326b3c IgdtSwap
void IgdtSwap() {
    if (g_gl.fbo == kNoFbo || g_gl.fbo_tex == kNoFbo) {
        if (!g_gl.drawn) return;
    } else {
        const float skin[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_gl.fbo_tex);
        StoreMvp(g_base);
        DrawTexturedQuad(g_gl.prog_skin, g_copy_quad, 1.0f, skin);
    }
    // TODO(phase3): stock passes the pending screen DAT_00973fbf to
    // 0x0002fcfc, which for screen 0xdf emits VControlTask signal 0x1a
    // (0x003ae8c0), then resets DAT_00973fbf to 0xff.
    eglSwapBuffers(g_gl.display, g_gl.surface);
    // TODO(phase3): DAT_00df2fb0 = 1 (read by the screenshot path).
    if (g_first_frame_pending) {
        // The first visible frame tells pwr_agent the HMI is up (0x000560e4).
        // It usually comes before VmfThread::start() connects, as in stock
        // (main starts the display task first), so that send fails and is
        // logged; the 0x81 audio mode re-sends wake state 2 once
        // IgdtFirstFrameSwapped() (VControlTask::PowersubsystemNotifyMsg).
        g_first_frame_pending = false;
        VPowerProxy::Instance()->SendWakeStateToPowerAgent(2);
    }
}

// 0x0096f1cc IgdtFirstFrameSwapped
bool IgdtFirstFrameSwapped() {
    return !g_first_frame_pending;
}
