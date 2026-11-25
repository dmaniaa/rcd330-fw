// File name is ours; stock name unknown.
//
// IGDT dynamic runtime state: the RAM copies stock keeps of every dynamic
// group / image / text record, the branch and state selections, and the 29
// display layers (0x00e5128c) that say which screen each layer shows. Page
// code mutates them through the setters below (stock 0x00322690..0x00324a24);
// the scene walk (igdt_scene.cpp) and touch hit-test read them. Portable and
// host-testable: no GL.
//
// DEVIATION: stock tracks changes in one dirty bitmap per record kind (bit
// per tag, e.g. 0x00a0fc28 for groups) to redraw only dirty rects; ours keeps
// one flag for the whole runtime (IgdtConsumeDirty) and redraws everything.
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_RUNTIME_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_RUNTIME_H

#include <stdint.h>

#include "display/igdt/igdt_tables.h"
#include "display/igdt/igdt_textbuf.h"   // IGDT_OK

enum {
    IGDT_LAYER_COUNT = 29,
    IGDT_SCREEN_NONE = 0xff,
    IGDT_BRANCH_DYN_COUNT = 0x2f0,      // position/tint/alpha slots of kinds 1, 3, 9
    IGDT_ERR_BRANCH_RANGE = 0x1003,     // 0x00322860: branch index >= 0x10f0
    IGDT_ERR_BAD_TAG = 0x1004,
    IGDT_ERR_STATE_RANGE = 0x1005,      // 0x00322900: state index >= 0x1b11
    IGDT_ERR_BAD_KIND = 0x1006,         // 0x00322860 / 0x00322900: wrong node kind
    IGDT_ERR_BAD_MATRIX_TAG = 0x1008,
    IGDT_ERR_RANGE = 0x10ff,
    IGDT_ERR_SCREEN = 0x80
};

// RAM 0x00a4bb14 + idx * 0x10 (alpha, x, y, touch area) with the tint and
// matrix (0x00a7c6b4) arrays.
struct IgdtDynGrpState {
    uint8_t alpha;
    int16_t x, y;
    uint32_t tint;              // 0xffffffff = none (0x0031dad0)
    IgdtClipRect touch_clip;    // clip of the children when the group clips, and touch area
    float matrix[9];
};

// RAM 0x00b054d0 + idx * 0xc (alpha, fg, x, y), tint array 0x00aea0dc, matrix 0x00b570ac.
struct IgdtDynImgState {
    uint8_t alpha;
    int16_t x, y;
    uint32_t tint;              // IgdtSetColor; 0xffffffff = none
    uint32_t fg;                // record +8 (draw state fg colour)
    float matrix[9];
};

// RAM 0x00a117a0 + idx * 0x10 (align byte, alpha, fg, x, y), matrix 0x00a1fa40.
struct IgdtDynTxtState {
    uint8_t alpha;
    uint8_t align_override;     // bits 0-1 horizontal, 2-3 background, 4-5 truncation
    bool has_align_override;    // set once IgdtSetTextAlign wrote align_override
    int16_t x, y;
    uint32_t fg_color;
    uint32_t bg_color;          // record +0xc (stock reads it from .rodata at draw)
    float matrix[9];
};

// Kinds 1 (branch), 3 (state) and 9 (screen) with the dynamic bit and index
// < 0x2f0: translation 0x00c4d2f0, tint 0x00c4c440, alpha 0x00c4d000.
struct IgdtBranchDynState {
    uint8_t alpha;
    int16_t x, y;
    uint32_t tint;
};

struct IgdtRuntime {
    uint8_t layers[IGDT_LAYER_COUNT];           // screen id per layer, IGDT_SCREEN_NONE = empty
    uint16_t branch[IGDT_BRANCH_COUNT];         // 0x00c502d8 selected child per branch slot
    uint16_t state[IGDT_STATE_COUNT];           // 0x00c524b8 selected child per state slot
    IgdtBranchDynState branch_dyn[IGDT_BRANCH_DYN_COUNT];
    IgdtDynGrpState dyn_grp[IGDT_DYN_GRP_COUNT];
    IgdtDynImgState dyn_img[IGDT_DYN_IMG_COUNT];
    IgdtDynTxtState dyn_txt[IGDT_DYN_TXT_COUNT];
    bool dirty;                                 // a value changed since IgdtConsumeDirty
};

// The process-wide runtime (stock: the RAM block at 0x00a117a0).
IgdtRuntime& IgdtGetRuntime();

// 0x00324328: every layer empty, branch/state selections from their init
// tables, every dynamic record from its .rodata record (0x00323c48,
// 0x00323dfc, 0x00323f70). Leaves dirty false.
void IgdtRuntimeInit(IgdtRuntime& rt, const IgdtSkinTables& t);

// Setters return IGDT_OK, also when the value does not change (dirty is set
// only on a change), or a stock error code; on an error nothing is written.

// 0x00322860 / 0x00322900: select child `child` of a branch (kind 1) / state
// (kind 3) slot.
int IgdtSetBranch(IgdtRuntime& rt, uint32_t tag, uint16_t child);
int IgdtSetState(IgdtRuntime& rt, uint32_t tag, uint16_t child);

// 0x00322a50: tint of a dynamic group / image (or kind 1/3/9 slot).
int IgdtSetColor(IgdtRuntime& rt, uint32_t tag, uint32_t argb);
// 0x00322bf8: alpha of a dynamic group / image / text (or kind 1/3/9 slot).
int IgdtSetAlpha(IgdtRuntime& rt, uint32_t tag, uint8_t alpha);

// 0x00322e18 / 0x00322ef4: touch area (and clip) of a dynamic group.
// STOCK-QUIRK: both return IGDT_OK for any other tag and do nothing.
int IgdtSetTouchArea(IgdtRuntime& rt, uint32_t tag, int16_t x, int16_t y, int16_t w, int16_t h);
int IgdtGetTouchArea(const IgdtRuntime& rt, uint32_t tag, IgdtClipRect* out);

// 0x00323494 / 0x003236f4 / 0x00323958 / 0x003239a0: position of a dynamic
// group / image / text (or kind 1/3/9 slot).
int IgdtSetPosX(IgdtRuntime& rt, uint32_t tag, int16_t x);
int IgdtSetPosY(IgdtRuntime& rt, uint32_t tag, int16_t y);
int IgdtSetPosXY(IgdtRuntime& rt, uint32_t tag, int16_t x, int16_t y);
int IgdtGetPosXY(const IgdtRuntime& rt, uint32_t tag, int16_t* x, int16_t* y);

// 0x00322690: horizontal align, background mode and truncation of dynamic text.
int IgdtSetTextAlign(IgdtRuntime& rt, uint32_t tag, uint8_t h_align, uint8_t bg_mode, uint8_t trunc_mode);

// 0x00324570 / 0x00324a24 / 0x0032499c: matrix of a dynamic group / image /
// text whose tag has a matrix sub-kind (bits 20..23 = 1, 3 or 4).
int IgdtSetMatrix(IgdtRuntime& rt, uint32_t tag, const float m[9]);
int IgdtSetScale(IgdtRuntime& rt, uint32_t tag, float sx, float sy);
int IgdtSetRotate(IgdtRuntime& rt, uint32_t tag, float radians);

// 0x003240e0: back to the .rodata record values.
int IgdtResetNode(IgdtRuntime& rt, const IgdtSkinTables& t, uint32_t tag);

// 0x0031bd30: the layer screen_id is shown on (scr_table +0); 0xff for
// 0xff or an unknown screen.
uint8_t IgdtScreenLayer(const IgdtSkinTables& t, uint8_t screen_id);

// 0x0031bd48: shows screen_id on its layer (scr_table +0), replacing the
// screen there. IGDT_ERR_SCREEN for 0xff or an unknown screen.
int IgdtLayerShowScreen(IgdtRuntime& rt, const IgdtSkinTables& t, uint8_t screen_id);
// Empties the layer of screen_id if it shows that screen. Name is ours.
int IgdtLayerHideScreen(IgdtRuntime& rt, const IgdtSkinTables& t, uint8_t screen_id);

// True (and cleared) when something changed since the last call. Name is ours.
bool IgdtConsumeDirty(IgdtRuntime& rt);

#endif
