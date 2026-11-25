// File name is ours; stock name unknown.
#include "display/igdt/igdt_scene.h"

#include "display/igdt/igdt_gl.h"
#include "display/igdt/igdt_strings.h"
#include "display/igdt/igdt_text.h"
#include "display/igdt/igdt_textbuf.h"

namespace {

const uint16_t kScreenW = 800;   // 0x00a0f94c
const uint16_t kScreenH = 480;   // 0x00a0f94e

// The walk's view of the tables and the runtime.
struct Scene {
    const IgdtSkinTables& t;
    const IgdtRuntime& rt;
};

unsigned KindOf(uint32_t tag) {
    return (tag >> 24) & 0xf;
}

bool IsDynamic(uint32_t tag) {
    return (tag & IGDT_TAG_DYNAMIC) != 0;
}

// The part of the draw state a node saves on entry and restores on exit
// (origin, alpha, tint, flags, matrix, clip; not pivot, font or colours).
struct SavedState {
    int16_t x, y;
    uint8_t alpha;
    uint32_t tint;
    uint8_t flags;
    float matrix[9];
    int16_t clip_x0, clip_y0, clip_x1, clip_y1;
};

// 0x0031d714 SaveState
void SaveState(const IgdtDrawState& s, SavedState& out) {
    out.x = s.x;
    out.y = s.y;
    out.alpha = s.alpha;
    out.tint = s.tint;
    out.flags = s.flags;
    for (int i = 0; i < 9; ++i) out.matrix[i] = s.matrix[i];
    out.clip_x0 = s.clip_x0;
    out.clip_y0 = s.clip_y0;
    out.clip_x1 = s.clip_x1;
    out.clip_y1 = s.clip_y1;
}

// 0x0031d7c0 RestoreState
void RestoreState(IgdtDrawState& s, const SavedState& in) {
    s.x = in.x;
    s.y = in.y;
    s.alpha = in.alpha;
    s.tint = in.tint;
    s.flags = in.flags;
    for (int i = 0; i < 9; ++i) s.matrix[i] = in.matrix[i];
    s.clip_x0 = in.clip_x0;
    s.clip_y0 = in.clip_y0;
    s.clip_x1 = in.clip_x1;
    s.clip_y1 = in.clip_y1;
}

// Origin 0, opaque, no tint, identity transform, full-screen clip.
// 0x0031d684 ResetState
void ResetState(IgdtDrawState& s) {
    s.x = 0;
    s.y = 0;
    s.alpha = 0xff;
    s.tint = 0xffffffffu;
    s.flags = 0;
    for (int i = 0; i < 9; ++i) s.matrix[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    s.clip_x0 = 0;
    s.clip_y0 = 0;
    s.clip_x1 = (int16_t)s.screen_w;
    s.clip_y1 = (int16_t)s.screen_h;
}

// 0x0031daa4 Translate
void Translate(IgdtDrawState& s, int16_t dx, int16_t dy) {
    s.x = (int16_t)(s.x + dx);
    s.y = (int16_t)(s.y + dy);
}

// Every scene caller passes modes (1, 2): an opaque colour other than
// white replaces the tint, 0xffffffff keeps the inherited one.
// 0x0031dad0 ApplyTint
void ApplyTint(IgdtDrawState& s, uint32_t color) {
    color |= 0xff000000u;
    if (color != 0xffffffffu) s.tint = color;
}

// Alpha 0 hides; 0xff keeps; otherwise alpha = alpha * a >> 8.
// 0x0031db3c ApplyAlpha
void ApplyAlpha(IgdtDrawState& s, uint8_t a) {
    s.flags &= (uint8_t)~IGDT_STATE_TRANSPARENT;
    if (a == 0) {
        s.alpha = 0;
        s.flags |= IGDT_STATE_TRANSPARENT;
        return;
    }
    if (a == 0xff) return;
    s.alpha = (uint8_t)((unsigned)s.alpha * a >> 8);
    if (s.alpha == 0) s.flags |= IGDT_STATE_TRANSPARENT;
}

// Intersects the clip with r (made absolute with the origin, 0x0031d9cc);
// no overlap leaves the empty clip (0, 0, 0, 0) and sets the clipped flag.
// STOCK-QUIRK: an overlapping result counts as empty only when it is
// (0, 0, 0, 0), and a non-empty result clears a clipped flag set by an
// outer group.
// 0x0031da6c ApplyClip (0x0031d86c intersection)
void ApplyClip(IgdtDrawState& s, const IgdtClipRect& r) {
    const int16_t rx0 = (int16_t)(s.x + r.x), ry0 = (int16_t)(s.y + r.y);
    const int16_t rx1 = (int16_t)(rx0 + r.w), ry1 = (int16_t)(ry0 + r.h);
    bool empty;
    if (s.clip_x0 <= rx1 && rx0 <= s.clip_x1 && s.clip_y0 <= ry1 && ry0 <= s.clip_y1) {
        if (s.clip_x0 < rx0) s.clip_x0 = rx0;
        if (rx1 < s.clip_x1) s.clip_x1 = rx1;
        if (s.clip_y0 < ry0) s.clip_y0 = ry0;
        if (ry1 < s.clip_y1) s.clip_y1 = ry1;
        empty = s.clip_x0 == 0 && s.clip_y0 == 0 && s.clip_x1 == 0 && s.clip_y1 == 0;
    } else {
        s.clip_x0 = 0;
        s.clip_y0 = 0;
        s.clip_x1 = 0;
        s.clip_y1 = 0;
        empty = true;
    }
    if (empty) {
        s.flags |= IGDT_STATE_CLIPPED;
    } else {
        s.flags &= (uint8_t)~IGDT_STATE_CLIPPED;
    }
}

// state matrix = state matrix * m (column-major); identity m is skipped.
// 0x0031df9c ApplyMatrix (0x0031dbac)
void ApplyMatrix(IgdtDrawState& s, const float* m) {
    static const float kIdentity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    bool identity = true;
    for (int i = 0; i < 9; ++i) identity = identity && m[i] == kIdentity[i];
    if (identity) return;
    float a[9];
    for (int i = 0; i < 9; ++i) a[i] = s.matrix[i];
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            s.matrix[col * 3 + row] =
                a[row] * m[col * 3] + a[3 + row] * m[col * 3 + 1] + a[6 + row] * m[col * 3 + 2];
        }
    }
}

// 0x0031db90 SetPivot
void SetPivot(IgdtDrawState& s, float x, float y) {
    s.pivot_x = x;
    s.pivot_y = y;
}

// Font style and the three 2-bit align fields of draw state +0x3f.
// 0x0031dfac SetTextStyle
void SetTextStyle(IgdtDrawState& s, uint8_t font, uint8_t h_align, uint8_t bg_mode, uint8_t trunc_mode) {
    s.text_align = (uint8_t)((s.text_align & 0xc0) | (h_align & 3) | (bg_mode & 3) << 2 | (trunc_mode & 3) << 4);
    s.font = font;
}

// Translation, tint and alpha of a dynamic kind 1/3/9 slot (index < 0x2f0).
void ApplySlot(IgdtDrawState& s, const IgdtBranchDynState& slot) {
    Translate(s, slot.x, slot.y);
    ApplyTint(s, slot.tint);
    ApplyAlpha(s, slot.alpha);
}

// The selected child of a branch (selection = rt.branch) or state
// (rt.state) node: NULL without children, for a slot past count, or when
// the selection is not below the child count.
// 0x00321ea8 SelectedChild (0x00321f00 for states)
const IgdtNode* SelectedChild(const Scene& sc, const IgdtNode& n, const uint16_t* selection, uint32_t count) {
    if (n.child_first < 0 || n.eidx >= count) return 0;
    const uint16_t child = selection[n.eidx];
    if (child >= n.child_count) return 0;
    return &sc.t.nodes[n.child_first + child];
}

void WalkNodes(const Scene& sc, int32_t first, uint16_t count);

// DEVIATION: indices past the generated tables (which the generator rules
// out) skip the node instead of reading past them, here and below.
// 0x0031f0a0 DrawImageNode
void DrawImageNode(const Scene& sc, const IgdtNode& n) {
    const IgdtSkinTables& t = sc.t;
    const uint32_t idx = n.tag & IGDT_TAG_INDEX_MASK;
    if (n.eidx >= t.tex_count) return;
    IgdtDrawState& s = IgdtCurrentState();
    SavedState saved;
    SaveState(s, saved);
    IgdtElement e = {IGDT_ELEM_IMG, n.tag, 0, 0, (int16_t)t.textures[n.eidx].w, (int16_t)t.textures[n.eidx].h,
                     0xff, 0xffffffffu, n.eidx, 0, 0, 0, 0, 0};
    const IgdtImgRecord* rec = 0;
    if (!IsDynamic(n.tag)) {
        if (idx < t.static_img_count) {
            rec = &t.static_imgs[idx];
            Translate(s, rec->x, rec->y);
            ApplyAlpha(s, rec->alpha);
            s.fg_color = rec->tint;   // 0x0031dfec
            ApplyMatrix(s, rec->matrix);
            e.x = rec->x;
            e.y = rec->y;
            e.alpha = rec->alpha;
            e.color = rec->tint;
        }
    } else if (idx < IGDT_DYN_IMG_COUNT) {
        const IgdtDynImgState& d = sc.rt.dyn_img[idx];
        rec = &t.dyn_imgs[idx];
        Translate(s, d.x, d.y);
        ApplyTint(s, d.tint);
        ApplyAlpha(s, d.alpha);
        s.fg_color = d.fg;
        ApplyMatrix(s, d.matrix);
        e.x = d.x;
        e.y = d.y;
        e.alpha = d.alpha;
        e.color = d.fg;
    }
    if (rec) {
        SetPivot(s, rec->pivot_x, rec->pivot_y);
        // Called even when s.flags != 0: stock 0x00326ee8 still updates the
        // scissor and MVP before its flags check (no pixel effect).
        IgdtDrawImage(t, e);
    }
    RestoreState(s, saved);
}

// Kind 4 reads the engineering table at the layout offset, kind 5 the
// runtime text buffer there (filled by page code via IgdtSetRuntimeText).
// 0x0031f41c DrawTextNode
void DrawTextNode(const Scene& sc, const IgdtNode& n) {
    const IgdtSkinTables& t = sc.t;
    const uint32_t idx = n.tag & IGDT_TAG_INDEX_MASK;
    const IgdtTextLayout& layout = t.text_layouts[n.eidx];   // generator: eidx in range
    const IgdtTxtRecord* rec = 0;
    IgdtDrawState& s = IgdtCurrentState();
    SavedState saved;
    SaveState(s, saved);
    IgdtElement e = {IGDT_ELEM_TXT, n.tag, 0, 0, (int16_t)layout.width, layout.max_chars, 0xff, 0, -1,
                     layout.font, layout.align, IGDT_STR_RUNTIME, layout.off, 0};
    if (!IsDynamic(n.tag)) {
        if (idx < t.static_txt_count) {
            rec = &t.static_txts[idx];
            Translate(s, rec->x, rec->y);
            ApplyAlpha(s, rec->alpha);
            SetTextStyle(s, layout.font, layout.align & 3, (layout.align >> 4) & 3, layout.align >> 6);
            s.fg_color = rec->fg_color;   // 0x0031dfec
            s.bg_color = rec->bg_color;   // 0x0031dffc
            ApplyMatrix(s, rec->matrix);
            e.x = rec->x;
            e.y = rec->y;
            e.alpha = rec->alpha;
        }
    } else if (idx < IGDT_DYN_TXT_COUNT) {
        const IgdtDynTxtState& d = sc.rt.dyn_txt[idx];
        rec = &t.dyn_txts[idx];
        Translate(s, d.x, d.y);
        ApplyAlpha(s, d.alpha);
        // Font of the record's own layout (RAM +1, set by 0x00323f70).
        const uint8_t a = d.align_override;
        SetTextStyle(s, t.text_layouts[rec->layout].font, a & 3, (a >> 2) & 3, (a >> 4) & 3);
        s.fg_color = d.fg_color;
        s.bg_color = d.bg_color;
        ApplyMatrix(s, d.matrix);
        e.x = d.x;
        e.y = d.y;
        e.alpha = d.alpha;
    }
    if (rec) {
        e.color = s.fg_color;
        e.bg_color = s.bg_color;
        const IgdtWchar* text;
        IgdtWchar eng[0x141];
        if (KindOf(n.tag) == IGDT_KIND_TXT) {
            // Stock reads the UTF-32 table in place; ours is generated as UTF-8.
            e.str_id = layout.off;
            e.rt_off = 0;
            IgdtUtf8ToUtf32(IgdtGetEngString(layout.off), eng, sizeof(eng) / sizeof(eng[0]));
            text = eng;
        } else {
            text = IgdtRuntimeText(t, layout.off);
        }
        if (text) {
            SetPivot(s, rec->pivot_x, rec->pivot_y);
            IgdtDrawText(e, text);
        }
    }
    RestoreState(s, saved);
}

// Static kind 7: record (alpha, x, y) by tag index, w/h/colour by eidx.
// 0x0031f8e4 DrawRectNode (kind-7 case)
void DrawRectNode(const Scene& sc, const IgdtNode& n) {
    const IgdtSkinTables& t = sc.t;
    const uint32_t idx = n.tag & IGDT_TAG_INDEX_MASK;
    if (idx >= t.rect_count || n.eidx >= t.rect_count) return;
    const IgdtRectRecord& rec = t.rects[idx];
    const IgdtRectRecord& box = t.rects[n.eidx];
    IgdtDrawState& s = IgdtCurrentState();
    SavedState saved;
    SaveState(s, saved);
    Translate(s, rec.x, rec.y);
    ApplyAlpha(s, rec.alpha);
    s.fg_color = box.color;   // 0x0031dfec
    // The record matrix and pivot are identity / zero in 5624 (generator check).
    SetPivot(s, 0.0f, 0.0f);
    IgdtDrawRect(box.w, box.h, box.color);
    RestoreState(s, saved);
}

// Only an img child of the selected state is drawn.
// 0x0031f8e4 DrawStateNode (kind-3 case)
void DrawStateNode(const Scene& sc, const IgdtNode& n) {
    IgdtDrawState& s = IgdtCurrentState();
    SavedState saved;
    SaveState(s, saved);
    const uint32_t idx = n.tag & IGDT_TAG_INDEX_MASK;
    // DEVIATION: stock does not check idx < 0x2f0 here (5624 stays below).
    if (IsDynamic(n.tag) && idx < IGDT_BRANCH_DYN_COUNT) ApplySlot(s, sc.rt.branch_dyn[idx]);
    const IgdtNode* child = SelectedChild(sc, n, sc.rt.state, IGDT_STATE_COUNT);
    if (child && KindOf(child->tag) == IGDT_KIND_IMG) DrawImageNode(sc, *child);
    RestoreState(s, saved);
}

// Kind 0 group: own clip, alpha, matrix, pivot (static) or the RAM
// translation, tint, alpha, touch-area clip, matrix (dynamic), then the
// children. Other kinds (reached as a branch or a branch's selected child):
// the selected child, walked as a group again.
// 0x003202c4 WalkGroup
void WalkGroup(const Scene& sc, const IgdtNode& n) {
    const IgdtSkinTables& t = sc.t;
    const uint32_t idx = n.tag & IGDT_TAG_INDEX_MASK;
    IgdtDrawState& s = IgdtCurrentState();
    SavedState saved;
    SaveState(s, saved);
    if (KindOf(n.tag) == IGDT_KIND_GROUP) {
        const IgdtGrpFlags* flags = n.eidx < t.grp_flag_count ? &t.grp_flags[n.eidx] : 0;
        bool ok = flags != 0;
        if (ok && !IsDynamic(n.tag)) {
            ok = idx < t.static_grp_count;
            if (ok) {
                const IgdtGrpRecord& rec = t.static_grps[idx];
                if ((flags->flags & 3) && flags->clip_idx < t.clip_count) ApplyClip(s, t.clips[flags->clip_idx]);
                ApplyAlpha(s, rec.alpha);
                ApplyMatrix(s, rec.matrix);
                SetPivot(s, rec.pivot_x, rec.pivot_y);
            }
        } else if (ok) {
            ok = idx < IGDT_DYN_GRP_COUNT;
            if (ok) {
                const IgdtDynGrpState& d = sc.rt.dyn_grp[idx];
                Translate(s, d.x, d.y);
                ApplyTint(s, d.tint);
                ApplyAlpha(s, d.alpha);
                if (flags->flags & 3) ApplyClip(s, d.touch_clip);
                ApplyMatrix(s, d.matrix);
                SetPivot(s, t.dyn_grps[idx].pivot_x, t.dyn_grps[idx].pivot_y);
            }
        }
        // Flow layout (flags & 0xc != 0xc, 0x00321cb4 / 0x0031ee54) needs
        // children; the generator rejects such groups that have any.
        if (ok && (flags->flags & 0xc) == 0xc) WalkNodes(sc, n.child_first, n.child_count);
    } else if (n.child_first >= 0 && idx < IGDT_BRANCH_COUNT) {
        if (idx < IGDT_BRANCH_DYN_COUNT && IsDynamic(n.tag)) ApplySlot(s, sc.rt.branch_dyn[idx]);
        const IgdtNode* child = SelectedChild(sc, n, sc.rt.branch, IGDT_BRANCH_COUNT);
        if (child) WalkGroup(sc, *child);
    }
    RestoreState(s, saved);
}

// 0x0031f8e4 WalkNodes
void WalkNodes(const Scene& sc, int32_t first, uint16_t count) {
    if (first < 0) return;
    for (uint16_t i = 0; i < count; ++i) {
        const IgdtNode& n = sc.t.nodes[first + i];
        switch (KindOf(n.tag)) {
        case IGDT_KIND_GROUP:
        case IGDT_KIND_BRANCH:
            WalkGroup(sc, n);
            break;
        case IGDT_KIND_IMG:
            DrawImageNode(sc, n);
            break;
        case IGDT_KIND_STATE:
            DrawStateNode(sc, n);
            break;
        case IGDT_KIND_TXT:
        case IGDT_KIND_TXT_DYN:
            DrawTextNode(sc, n);
            break;
        case IGDT_KIND_RECT:
            if (!IsDynamic(n.tag)) DrawRectNode(sc, n);   // dynamic kind 7 draws nothing
            break;
        default:
            // TODO(phase3): kind 8 (runtime image, 0x00133734 / 0x00326e4c).
            break;
        }
    }
}

// One screen of a layer.
// 0x0032072c DrawScreen (per-screen body)
void DrawScreen(const Scene& sc, const IgdtScreen& screen) {
    IgdtDrawState& s = IgdtCurrentState();
    ResetState(s);
    // TODO(phase3): the display layer's RAM alpha and offset
    // (DAT_00a0fa34 + layer + 0x23c714, + (layer + 0x8f1c4) * 4) and, when
    // scr_table +1 is 0, the kind-9 slot 0 alpha and offset.
    WalkNodes(sc, (int32_t)screen.root_first, screen.root_count);
}

} // namespace

// DEVIATION: stock redraws only the dirty rects of each display layer's
// screen list (0x0031e69c / 0x0031ecc8), clipping each pass to its rect;
// ours redraws every layer's screen in full. A full redraw starts from a
// cleared target, as the first frame does: blending translucent elements
// (e.g. button glows over black) onto the previous frame again would
// brighten them on every redraw, which stock's rect redraws never do to
// pixels outside the dirty rects.
// 0x0032072c IgdtSceneDrawLayers
bool IgdtSceneDrawLayers(const IgdtSkinTables& t, const IgdtRuntime& rt) {
    IgdtDrawState& s = IgdtCurrentState();
    s.screen_w = kScreenW;
    s.screen_h = kScreenH;
    ResetState(s);
    IgdtBeginFrame();
    IgdtClearFrame();
    const Scene sc = {t, rt};
    bool drawn = false;
    for (int layer = 0; layer < IGDT_LAYER_COUNT; ++layer) {
        const uint8_t sid = rt.layers[layer];
        if (sid == IGDT_SCREEN_NONE || sid >= t.screen_count) continue;
        DrawScreen(sc, t.screens[sid]);
        drawn = true;
    }
    return drawn;
}
