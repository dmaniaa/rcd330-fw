// File name is ours; stock name unknown.
#include "display/igdt/igdt_runtime.h"

#include <math.h>
#include <string.h>

namespace {

const uint32_t kKindGroup = (uint32_t)IGDT_KIND_GROUP << 24;
const uint32_t kKindBranch = (uint32_t)IGDT_KIND_BRANCH << 24;
const uint32_t kKindImg = (uint32_t)IGDT_KIND_IMG << 24;
const uint32_t kKindState = (uint32_t)IGDT_KIND_STATE << 24;
const uint32_t kKindTxt = (uint32_t)IGDT_KIND_TXT << 24;
const uint32_t kKindTxtDyn = (uint32_t)IGDT_KIND_TXT_DYN << 24;
const uint32_t kKindImg8 = (uint32_t)IGDT_KIND_IMG8 << 24;
const uint32_t kKindScreen = (uint32_t)IGDT_KIND_SCREEN << 24;
const uint32_t kKindB = 0x0b000000u;   // accepted and ignored by 0x00322bf8 / 0x00323494
const uint32_t kImg8Count = 0x79;      // 0x00324328 kind-8 loop

IgdtRuntime g_runtime;

uint32_t Kind(uint32_t tag) {
    return tag & IGDT_TAG_KIND_MASK;
}

uint32_t Index(uint32_t tag) {
    return tag & IGDT_TAG_INDEX_MASK;
}

bool IsDynamic(uint32_t tag) {
    return (tag & IGDT_TAG_DYNAMIC) != 0;
}

// The record classes every dynamic setter tells apart, in stock order.
bool IsGrp(uint32_t tag) {
    return Index(tag) < IGDT_DYN_GRP_COUNT && Kind(tag) == kKindGroup;
}

bool IsImg(uint32_t tag) {
    return Index(tag) < IGDT_DYN_IMG_COUNT && Kind(tag) == kKindImg;
}

bool IsImg8(uint32_t tag) {
    return Index(tag) < kImg8Count && Kind(tag) == kKindImg8;
}

bool IsTxtKind(uint32_t tag) {
    return Kind(tag) == kKindTxt || Kind(tag) == kKindTxtDyn;
}

bool IsTxt(uint32_t tag) {
    return IsTxtKind(tag) && Index(tag) < IGDT_DYN_TXT_COUNT;
}

bool IsSlotKind(uint32_t tag) {
    return Kind(tag) == kKindBranch || Kind(tag) == kKindState || Kind(tag) == kKindScreen;
}

bool IsSlot(uint32_t tag) {
    return IsSlotKind(tag) && Index(tag) < IGDT_BRANCH_DYN_COUNT;
}

// Tag sub-kind 1, 3 or 4: the record has a RAM matrix (0x00324818).
bool HasMatrix(uint32_t tag) {
    const uint32_t sub = tag & 0x00f00000u;
    return sub == 0x00100000u || sub == 0x00300000u || sub == 0x00400000u;
}

// Writes value when it differs; true on a change.
template <typename T>
bool Set(T& field, T value) {
    if (field == value) return false;
    field = value;
    return true;
}

// Set, and rt.dirty on a change.
template <typename T>
void Store(IgdtRuntime& rt, T& field, T value) {
    if (Set(field, value)) rt.dirty = true;
}

// 0x00323b34: copies the 3x3 element-wise; true when one differed.
bool CopyMatrix(float* dst, const float* src) {
    bool changed = false;
    for (int i = 0; i < 9; ++i) {
        if (dst[i] != src[i]) {
            dst[i] = src[i];
            changed = true;
        }
    }
    return changed;
}

// DEVIATION: 0x00324818 / 0x00324570 index the matrix arrays without a
// bounds check; ours returns NULL for an index past the record count. NULL
// too for kinds without a matrix and for kind 8 (TODO(phase3): the 0x79
// kind-8 records, 0x00c4f1d4).
float* MatrixOf(IgdtRuntime& rt, uint32_t tag) {
    const uint32_t i = Index(tag);
    if (IsTxtKind(tag)) return i < IGDT_DYN_TXT_COUNT ? rt.dyn_txt[i].matrix : 0;
    if (Kind(tag) == kKindGroup) return i < IGDT_DYN_GRP_COUNT ? rt.dyn_grp[i].matrix : 0;
    if (Kind(tag) == kKindImg) return i < IGDT_DYN_IMG_COUNT ? rt.dyn_img[i].matrix : 0;
    return 0;
}

// 0x00323c48 InitDynGroup
bool InitDynGroup(IgdtRuntime& rt, const IgdtSkinTables& t, uint32_t i) {
    const IgdtGrpRecord& rec = t.dyn_grps[i];
    IgdtDynGrpState& d = rt.dyn_grp[i];
    bool changed = false;
    changed |= Set(d.alpha, rec.alpha);
    changed |= Set(d.tint, 0xffffffffu);
    // Key 0 or clip_table index 0: (0, 0, -1, -1). The generator keeps every
    // key below grp_flag_count and every index below clip_count.
    IgdtClipRect clip = {0, 0, -1, -1};
    const uint16_t clip_idx = rec.key < t.grp_flag_count ? t.grp_flags[rec.key].clip_idx : 0;
    if (clip_idx != 0 && clip_idx < t.clip_count) clip = t.clips[clip_idx];
    IgdtClipRect& c = d.touch_clip;
    if (c.x != clip.x || c.y != clip.y || c.w != clip.w || c.h != clip.h) {
        c = clip;
        changed = true;
    }
    // STOCK-QUIRK: x/y are not reset; they start at 0 (.bss), not at the
    // record's +4/+6.
    changed |= CopyMatrix(d.matrix, rec.matrix);
    return changed;
}

// 0x00323dfc InitDynImage (kind 2 half)
bool InitDynImage(IgdtRuntime& rt, const IgdtSkinTables& t, uint32_t i) {
    const IgdtImgRecord& rec = t.dyn_imgs[i];
    IgdtDynImgState& d = rt.dyn_img[i];
    bool changed = CopyMatrix(d.matrix, rec.matrix);
    changed |= Set(d.alpha, rec.alpha);
    changed |= Set(d.tint, 0xffffffffu);
    changed |= Set(d.fg, rec.tint);
    changed |= Set(d.x, rec.x);
    changed |= Set(d.y, rec.y);
    return changed;
}

// The RAM align byte from text_layout +8 (0x0031dfac field order).
uint8_t LayoutAlignBits(uint8_t old, uint8_t align) {
    return (uint8_t)((old & 0xc0) | (align & 3) | ((align >> 4) & 3) << 2 | (align >> 6) << 4);
}

// 0x00323f70 InitDynText
bool InitDynText(IgdtRuntime& rt, const IgdtSkinTables& t, uint32_t i, bool with_matrix) {
    const IgdtTxtRecord& rec = t.dyn_txts[i];
    const IgdtTextLayout& layout = t.text_layouts[rec.layout];   // generator: layout in range
    IgdtDynTxtState& d = rt.dyn_txt[i];
    bool changed = false;
    const uint8_t align = LayoutAlignBits(d.align_override, layout.align);
    changed |= Set(d.align_override, align);
    d.has_align_override = false;
    changed |= Set(d.alpha, rec.alpha);
    changed |= Set(d.fg_color, rec.fg_color);
    changed |= Set(d.x, rec.x);
    changed |= Set(d.y, rec.y);
    d.bg_color = rec.bg_color;
    if (with_matrix) changed |= CopyMatrix(d.matrix, rec.matrix);
    return changed;
}

// Kinds 1, 3, 9: opaque, untinted, at 0, 0.
// 0x003240e0 ResetSlot (also the 0x00324328 init loop)
bool InitSlot(IgdtBranchDynState& s) {
    bool changed = false;
    changed |= Set(s.alpha, (uint8_t)0xff);
    changed |= Set(s.tint, 0xffffffffu);
    changed |= Set(s.x, (int16_t)0);
    changed |= Set(s.y, (int16_t)0);
    return changed;
}

} // namespace

IgdtRuntime& IgdtGetRuntime() {
    return g_runtime;
}

// DEVIATION: starts from a zeroed block (stock's RAM is .bss, zero when this
// runs once at start-up).
// 0x00324328 IgdtRuntimeInit
void IgdtRuntimeInit(IgdtRuntime& rt, const IgdtSkinTables& t) {
    memset(&rt, 0, sizeof(rt));
    for (int i = 0; i < IGDT_BRANCH_DYN_COUNT; ++i) InitSlot(rt.branch_dyn[i]);
    for (uint32_t i = 0; i < IGDT_DYN_IMG_COUNT; ++i) InitDynImage(rt, t, i);
    // TODO(phase3): the 0x79 kind-8 records (0x00323dfc(0x8000000, i)).
    for (uint32_t i = 0; i < IGDT_DYN_GRP_COUNT; ++i) InitDynGroup(rt, t, i);
    for (uint32_t i = 0; i < IGDT_DYN_TXT_COUNT; ++i) InitDynText(rt, t, i, true);
    memcpy(rt.branch, t.branch_init, sizeof(rt.branch));
    memcpy(rt.state, t.state_init, sizeof(rt.state));
    memset(rt.layers, IGDT_SCREEN_NONE, sizeof(rt.layers));
    rt.dirty = false;
}

// 0x00322860 IgdtSetBranch
int IgdtSetBranch(IgdtRuntime& rt, uint32_t tag, uint16_t child) {
    if (Kind(tag) != kKindBranch) return IGDT_ERR_BAD_KIND;
    if (Index(tag) >= IGDT_BRANCH_COUNT) return IGDT_ERR_BRANCH_RANGE;
    Store(rt, rt.branch[Index(tag)], child);
    return IGDT_OK;
}

// 0x00322900 IgdtSetState
int IgdtSetState(IgdtRuntime& rt, uint32_t tag, uint16_t child) {
    if (Kind(tag) != kKindState) return IGDT_ERR_BAD_KIND;
    if (Index(tag) >= IGDT_STATE_COUNT) return IGDT_ERR_STATE_RANGE;
    Store(rt, rt.state[Index(tag)], child);
    return IGDT_OK;
}

// 0x00322a50 IgdtSetColor
int IgdtSetColor(IgdtRuntime& rt, uint32_t tag, uint32_t argb) {
    if (!IsDynamic(tag)) return IGDT_ERR_BAD_TAG;
    const uint32_t i = Index(tag);
    if (IsGrp(tag)) {
        Store(rt, rt.dyn_grp[i].tint, argb);
    } else if (IsImg(tag)) {
        Store(rt, rt.dyn_img[i].tint, argb);
    } else if (IsSlot(tag)) {
        Store(rt, rt.branch_dyn[i].tint, argb);
    } else {
        // TODO(phase3): kind 8 (0x00c4ea44); IGDT_ERR_BAD_TAG until then.
        return IGDT_ERR_BAD_TAG;
    }
    return IGDT_OK;
}

// 0x00322bf8 IgdtSetAlpha
int IgdtSetAlpha(IgdtRuntime& rt, uint32_t tag, uint8_t alpha) {
    if (!IsDynamic(tag)) return IGDT_ERR_BAD_TAG;
    const uint32_t i = Index(tag);
    if (IsGrp(tag)) {
        Store(rt, rt.dyn_grp[i].alpha, alpha);
    } else if (IsImg(tag)) {
        Store(rt, rt.dyn_img[i].alpha, alpha);
    } else if (IsImg8(tag)) {
        return IGDT_ERR_BAD_TAG;   // TODO(phase3): kind 8 (0x00c4ec28)
    } else if (IsTxt(tag)) {
        Store(rt, rt.dyn_txt[i].alpha, alpha);
    } else if (Kind(tag) == kKindB) {
        return IGDT_OK;
    } else if (IsSlot(tag)) {
        Store(rt, rt.branch_dyn[i].alpha, alpha);
    } else {
        return IGDT_ERR_BAD_TAG;
    }
    return IGDT_OK;
}

// 0x00322e18 IgdtSetTouchArea
int IgdtSetTouchArea(IgdtRuntime& rt, uint32_t tag, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!IsDynamic(tag) || !IsGrp(tag)) return IGDT_OK;
    IgdtClipRect& c = rt.dyn_grp[Index(tag)].touch_clip;
    if (c.x == x && c.y == y && c.w == w && c.h == h) return IGDT_OK;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    rt.dirty = true;
    return IGDT_OK;
}

// DEVIATION: a NULL out returns IGDT_ERR_RANGE (stock dereferences it).
// 0x00322ef4 IgdtGetTouchArea
int IgdtGetTouchArea(const IgdtRuntime& rt, uint32_t tag, IgdtClipRect* out) {
    if (!out) return IGDT_ERR_RANGE;
    if (IsDynamic(tag) && IsGrp(tag)) *out = rt.dyn_grp[Index(tag)].touch_clip;
    return IGDT_OK;
}

namespace {

// Axis 0 = x, 1 = y.
// 0x00323494 SetPos (0x003236f4 for y)
int SetPos(IgdtRuntime& rt, uint32_t tag, int axis, int16_t v) {
    if (!IsDynamic(tag)) return IGDT_ERR_BAD_TAG;
    const uint32_t i = Index(tag);
    if (IsGrp(tag)) {
        Store(rt, axis ? rt.dyn_grp[i].y : rt.dyn_grp[i].x, v);
    } else if (IsImg(tag)) {
        Store(rt, axis ? rt.dyn_img[i].y : rt.dyn_img[i].x, v);
    } else if (IsImg8(tag)) {
        return IGDT_ERR_BAD_TAG;   // TODO(phase3): kind 8 (0x00c4ec30)
    } else if (IsTxt(tag)) {
        Store(rt, axis ? rt.dyn_txt[i].y : rt.dyn_txt[i].x, v);
    } else if (Kind(tag) == kKindB) {
        return IGDT_OK;
    } else if (IsSlot(tag)) {
        Store(rt, axis ? rt.branch_dyn[i].y : rt.branch_dyn[i].x, v);
    } else {
        return IGDT_ERR_BAD_TAG;
    }
    return IGDT_OK;
}

} // namespace

// 0x00323494 IgdtSetPosX
int IgdtSetPosX(IgdtRuntime& rt, uint32_t tag, int16_t x) {
    return SetPos(rt, tag, 0, x);
}

// 0x003236f4 IgdtSetPosY
int IgdtSetPosY(IgdtRuntime& rt, uint32_t tag, int16_t y) {
    return SetPos(rt, tag, 1, y);
}

// 0x00323958 IgdtSetPosXY
int IgdtSetPosXY(IgdtRuntime& rt, uint32_t tag, int16_t x, int16_t y) {
    if (!IsDynamic(tag)) return IGDT_ERR_RANGE;
    const int ret = IgdtSetPosX(rt, tag, x);
    if (ret != IGDT_OK) return ret;
    return IgdtSetPosY(rt, tag, y);
}

// DEVIATION: NULL x or y returns IGDT_ERR_RANGE (stock dereferences them).
// 0x003239a0 IgdtGetPosXY
int IgdtGetPosXY(const IgdtRuntime& rt, uint32_t tag, int16_t* x, int16_t* y) {
    if (!x || !y) return IGDT_ERR_RANGE;
    if (!IsDynamic(tag)) return IGDT_ERR_BAD_TAG;
    const uint32_t i = Index(tag);
    if (IsGrp(tag)) {
        *x = rt.dyn_grp[i].x;
        *y = rt.dyn_grp[i].y;
    } else if (IsImg(tag)) {
        *x = rt.dyn_img[i].x;
        *y = rt.dyn_img[i].y;
    } else if (IsTxt(tag)) {
        *x = rt.dyn_txt[i].x;
        *y = rt.dyn_txt[i].y;
    } else if (IsSlot(tag)) {
        *x = rt.branch_dyn[i].x;
        *y = rt.branch_dyn[i].y;
    } else {
        // TODO(phase3): kind 8 (0x00c4ec30); IGDT_ERR_BAD_TAG until then.
        return IGDT_ERR_BAD_TAG;
    }
    return IGDT_OK;
}

// 0x00322690 IgdtSetTextAlign
int IgdtSetTextAlign(IgdtRuntime& rt, uint32_t tag, uint8_t h_align, uint8_t bg_mode, uint8_t trunc_mode) {
    if (!IsDynamic(tag) || !IsTxt(tag)) return IGDT_ERR_RANGE;
    IgdtDynTxtState& d = rt.dyn_txt[Index(tag)];
    uint8_t v = d.align_override;
    // STOCK-QUIRK: each field compares the 2-bit value with the full
    // argument, so an argument > 3 always counts as a change.
    if ((v & 3) != h_align) v = (uint8_t)((v & 0xfc) | (h_align & 3)), rt.dirty = true;
    if (((v >> 2) & 3) != bg_mode) v = (uint8_t)((v & 0xf3) | (bg_mode & 3) << 2), rt.dirty = true;
    if (((v >> 4) & 3) != trunc_mode) v = (uint8_t)((v & 0xcf) | (trunc_mode & 3) << 4), rt.dirty = true;
    d.align_override = v;
    d.has_align_override = true;
    return IGDT_OK;
}

// STOCK-QUIRK: a matrix tag of any other kind returns IGDT_OK (no write).
// 0x00324570 IgdtSetMatrix
int IgdtSetMatrix(IgdtRuntime& rt, uint32_t tag, const float m[9]) {
    if (!HasMatrix(tag) || !m || !IsDynamic(tag)) return IGDT_ERR_BAD_MATRIX_TAG;
    const uint32_t kind = Kind(tag);
    if (kind != kKindTxt && kind != kKindTxtDyn && kind != kKindGroup && kind != kKindImg) {
        // TODO(phase3): kind 8 (0x00c4f1d4).
        return IGDT_OK;
    }
    float* dst = MatrixOf(rt, tag);
    if (!dst) return IGDT_ERR_BAD_TAG;   // DEVIATION: index past the record count
    if (CopyMatrix(dst, m)) rt.dirty = true;
    return IGDT_OK;
}

// STOCK-QUIRK: marks the node dirty even when the scale does not change
// (0x00324818 sets the dirty bit before returning the matrix).
// 0x00324a24 IgdtSetScale
int IgdtSetScale(IgdtRuntime& rt, uint32_t tag, float sx, float sy) {
    if (!HasMatrix(tag) || !IsDynamic(tag)) return IGDT_ERR_BAD_MATRIX_TAG;
    float* m = MatrixOf(rt, tag);
    if (!m) return IGDT_ERR_BAD_TAG;
    m[0] = sx;
    m[4] = sy;
    rt.dirty = true;
    return IGDT_OK;
}

// Rotation part of the matrix, column-major: (cos, sin, -, -sin, cos).
// STOCK-QUIRK: always dirty, as IgdtSetScale.
// 0x0032499c IgdtSetRotate
int IgdtSetRotate(IgdtRuntime& rt, uint32_t tag, float radians) {
    if (!HasMatrix(tag) || !IsDynamic(tag)) return IGDT_ERR_BAD_MATRIX_TAG;
    float* m = MatrixOf(rt, tag);
    if (!m) return IGDT_ERR_BAD_TAG;
    const float s = sinf(radians), c = cosf(radians);
    m[0] = c;
    m[1] = s;
    m[3] = -s;
    m[4] = c;
    rt.dirty = true;
    return IGDT_OK;
}

// 0x003240e0 IgdtResetNode
int IgdtResetNode(IgdtRuntime& rt, const IgdtSkinTables& t, uint32_t tag) {
    if (!IsDynamic(tag)) return IGDT_ERR_BAD_TAG;
    const uint32_t i = Index(tag);
    bool changed;
    if (IsGrp(tag)) {
        changed = InitDynGroup(rt, t, i);
    } else if (IsImg(tag)) {
        changed = InitDynImage(rt, t, i);
    } else if (IsTxt(tag)) {
        changed = InitDynText(rt, t, i, HasMatrix(tag));
    } else if (IsSlot(tag)) {
        changed = InitSlot(rt.branch_dyn[i]);
    } else {
        // TODO(phase3): kind 8 (0x00323dfc(0x8000000, i)); IGDT_ERR_BAD_TAG until then.
        return IGDT_ERR_BAD_TAG;
    }
    if (changed) rt.dirty = true;
    return IGDT_OK;
}

// DEVIATION: stock 0x0031bd48 inserts the screen into the slot list of its
// display layer (0x00452764 / 0x0031d130); ours keeps one screen per layer,
// the scr_table +0 layer group.
// DEVIATION: stock does not range-check screen_id.
// 0x0031bd30 IgdtScreenLayer
uint8_t IgdtScreenLayer(const IgdtSkinTables& t, uint8_t screen_id) {
    if (screen_id == IGDT_SCREEN_NONE || screen_id >= t.screen_count) return IGDT_SCREEN_NONE;
    return t.screens[screen_id].layer_group;
}

// 0x0031bd48 IgdtLayerShowScreen
int IgdtLayerShowScreen(IgdtRuntime& rt, const IgdtSkinTables& t, uint8_t screen_id) {
    if (screen_id == IGDT_SCREEN_NONE || screen_id >= t.screen_count) return IGDT_ERR_SCREEN;
    const uint8_t layer = t.screens[screen_id].layer_group;
    if (layer >= IGDT_LAYER_COUNT) return IGDT_ERR_SCREEN;
    Store(rt, rt.layers[layer], screen_id);
    return IGDT_OK;
}

int IgdtLayerHideScreen(IgdtRuntime& rt, const IgdtSkinTables& t, uint8_t screen_id) {
    if (screen_id == IGDT_SCREEN_NONE || screen_id >= t.screen_count) return IGDT_ERR_SCREEN;
    const uint8_t layer = t.screens[screen_id].layer_group;
    if (layer >= IGDT_LAYER_COUNT) return IGDT_ERR_SCREEN;
    if (rt.layers[layer] == screen_id) Store(rt, rt.layers[layer], (uint8_t)IGDT_SCREEN_NONE);
    return IGDT_OK;
}

bool IgdtConsumeDirty(IgdtRuntime& rt) {
    const bool dirty = rt.dirty;
    rt.dirty = false;
    return dirty;
}
