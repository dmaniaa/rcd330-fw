// File name is ours; stock name unknown.
#include "display/igdt/igdt_touch.h"

namespace {

uint32_t KindOf(uint32_t tag) {
    return (tag & IGDT_TAG_KIND_MASK) >> 24;
}

bool IsDynamic(uint32_t tag) {
    return (tag & IGDT_TAG_DYNAMIC) != 0;
}

// The hit test's slice of the draw state: the origin (0x00a0f908) and the
// last group rect made absolute with it (0x00a0fa28, set by 0x0031d86c
// before it intersects the clip, so ancestors do not clip it).
struct HitWalk {
    int16_t ox, oy;
    int16_t x0, y0, x1, y1;
    bool has_rect;
};

void Translate(HitWalk& w, int16_t dx, int16_t dy) {
    w.ox = (int16_t)(w.ox + dx);
    w.oy = (int16_t)(w.oy + dy);
}

// DEVIATION: a rect with w <= 0 or h <= 0 never hits. Stock's edges are
// inclusive, so it would hit the single pixel (x0, y0) of a zero-size area.
void SetRect(HitWalk& w, const IgdtClipRect& r) {
    w.x0 = (int16_t)(w.ox + r.x);
    w.y0 = (int16_t)(w.oy + r.y);
    w.x1 = (int16_t)(w.x0 + r.w);
    w.y1 = (int16_t)(w.y0 + r.h);
    w.has_rect = r.w > 0 && r.h > 0;
}

bool Inside(const HitWalk& w, int32_t x, int32_t y) {
    return w.has_rect && w.x0 <= x && x <= w.x1 && w.y0 <= y && y <= w.y1;
}

// Static group: its clip_table rect. Dynamic group: its RAM translation,
// then its touch area. Dynamic branch: its slot translation. Others: nothing.
// DEVIATION: indices past the generated tables skip the node (stock reads
// past them; the generator rules them out).
// 0x0032199c ApplyTouchNode
void ApplyTouchNode(HitWalk& w, const IgdtSkinTables& t, const IgdtRuntime& rt, const IgdtNode& n) {
    const uint32_t idx = n.tag & IGDT_TAG_INDEX_MASK;
    const uint32_t kind = KindOf(n.tag);
    if (!IsDynamic(n.tag)) {
        if (kind == IGDT_KIND_GROUP && n.eidx < t.grp_flag_count) {
            const uint16_t clip = t.grp_flags[n.eidx].clip_idx;
            if (clip < t.clip_count) SetRect(w, t.clips[clip]);
        }
    } else if (kind == IGDT_KIND_GROUP) {
        if (idx < IGDT_DYN_GRP_COUNT) {
            const IgdtDynGrpState& d = rt.dyn_grp[idx];
            Translate(w, d.x, d.y);
            SetRect(w, d.touch_clip);
        }
    } else if (kind == IGDT_KIND_BRANCH) {
        if (idx < IGDT_BRANCH_DYN_COUNT) Translate(w, rt.branch_dyn[idx].x, rt.branch_dyn[idx].y);
    }
}

// The live child of a non-group node: rt.branch[eidx] for every kind (no
// state node lies on a 5624 touch path). NULL without children or when the
// selection is not below the child count.
const IgdtNode* SelectedChild(const IgdtSkinTables& t, const IgdtRuntime& rt, const IgdtNode& n) {
    if (n.child_first < 0 || n.eidx >= IGDT_BRANCH_COUNT) return 0;
    const uint16_t child = rt.branch[n.eidx];
    if (child >= n.child_count) return 0;
    return &t.nodes[n.child_first + child];
}

// Follows one touch path of a screen: byte 0 picks a root, each further
// byte a child of the current node, until a byte >= its child count ends
// the path there. A group ending the path hits with its own rect, a branch
// with the rect of its selected child. Steps through a branch must take the
// selected child. Returns the hit node (rect in w) or NULL.
// 0x00324b90 IgdtTouchProcessQueue (per-path walk)
const IgdtNode* HitPath(HitWalk& w, const IgdtSkinTables& t, const IgdtRuntime& rt, const IgdtScreen& scr,
                        const IgdtTouchPath& path, int32_t x, int32_t y) {
    if (path.step_count < 2 || path.step_first + path.step_count > t.touch_step_count) return 0;
    const uint8_t* steps = &t.touch_steps[path.step_first];
    if (steps[0] >= scr.root_count) return 0;
    const IgdtNode* node = &t.nodes[scr.root_first + steps[0]];
    for (uint32_t i = 1; i < path.step_count; ++i) {
        const uint8_t step = steps[i];
        ApplyTouchNode(w, t, rt, *node);
        if (KindOf(node->tag) == IGDT_KIND_GROUP) {
            if (step >= node->child_count) return Inside(w, x, y) ? node : 0;
            if (node->child_first < 0) return 0;
            node = &t.nodes[node->child_first + step];
            continue;
        }
        const IgdtNode* selected = SelectedChild(t, rt, *node);
        if (step >= node->child_count) {
            if (!selected) return 0;
            ApplyTouchNode(w, t, rt, *selected);
            // DEVIATION: on a miss stock walks on from the selected child with
            // the bytes after the path's terminator (LAB_00324da0); ours ends
            // the path.
            return Inside(w, x, y) ? node : 0;
        }
        if (node->child_first < 0 || &t.nodes[node->child_first + step] != selected) return 0;
        node = selected;
    }
    return 0;   // every generated path ends with its terminator
}

} // namespace

void IgdtTouchInit(IgdtTouchState& ts) {
    ts.count = 0;
    ts.tail = 0;
    ts.head = 0;
    ts.captured = 0;
    ts.cap_x0 = 0;
    ts.cap_y0 = 0;
    ts.cap_x1 = 0;
    ts.cap_y1 = 0;
    // DEVIATION: stock's .bss starts at 0, then each release sets 0xff.
    ts.captured_screen = IGDT_SCREEN_NONE;
}

// 0x00324aec IgdtTouchEnqueue
bool IgdtTouchEnqueue(IgdtTouchState& ts, const IgdtTouchEvent& ev) {
    if (ts.count >= IGDT_TOUCH_QUEUE_LEN) return false;
    ts.queue[ts.tail] = ev;
    ts.tail = ts.tail + 1 >= IGDT_TOUCH_QUEUE_LEN ? 0 : ts.tail + 1;
    ++ts.count;
    return true;
}

// 0x000610a8 hmi_wrapper_igdt_insert_page (captured-touch part)
void IgdtTouchInvalidateForScreen(IgdtTouchState& ts, uint8_t screen_id) {
    if (!ts.captured) return;
    if (ts.captured_screen == screen_id || screen_id == IGDT_SCREEN_NONE) {
        ts.cap_x0 = 0;
        ts.cap_y0 = 0;
        ts.cap_x1 = -2;
        ts.cap_y1 = -2;
    }
}

// TODO(phase3): stock also rejects a hit whose rect a screen on a higher
// layer covers (0x003217d8: that screen's drawn bounding box, 0x003201e8,
// unless the page manager flags the screen, 0x000522ec).
// TODO(phase3): the display layer offset (0x00c4deb0) and, when scr_table
// +1 is 0, the kind-9 slot 0 offset start the origin, as in DrawScreen
// (igdt_scene.cpp).
// 0x00324b90 IgdtTouchProcessQueue
void IgdtTouchProcessQueue(IgdtTouchState& ts, const IgdtSkinTables& t, const IgdtRuntime& rt,
                           IgdtTouchCallback cb, void* ctx) {
    while (ts.count != 0) {
        IgdtTouchEvent ev = ts.queue[ts.head];
        ts.head = ts.head + 1 >= IGDT_TOUCH_QUEUE_LEN ? 0 : ts.head + 1;
        --ts.count;

        if (ts.captured) {
            if (ev.x < ts.cap_x0 || ts.cap_x1 < ev.x || ev.y < ts.cap_y0 || ts.cap_y1 < ev.y) {
                if (ts.cap_x0 == 0 && ts.cap_x1 == -2 && ts.cap_y0 == 0 && ts.cap_y1 == -2) {
                    ev.type |= IGDT_TOUCH_CANCEL_PAGE;
                } else {
                    // STOCK-QUIRK: the rect collapses for good, so coming
                    // back onto the node does not clear the cancel.
                    ts.cap_x0 = 0;
                    ts.cap_y0 = 0;
                    ts.cap_x1 = -1;
                    ts.cap_y1 = -1;
                    ev.type |= IGDT_TOUCH_CANCEL_OUTSIDE;
                }
            }
            if (cb) cb(ts.captured->tag, ev, ctx);
            if ((ev.type & 0xff) == IGDT_TOUCH_RELEASE) {
                ts.captured = 0;
                ts.captured_screen = IGDT_SCREEN_NONE;
            }
            continue;
        }
        if ((ev.type & 0xff) != IGDT_TOUCH_PRESS) continue;

        bool hit = false;
        for (int layer = IGDT_LAYER_COUNT - 1; layer >= 0 && !hit; --layer) {
            const uint8_t sid = rt.layers[layer];
            if (sid == IGDT_SCREEN_NONE || sid >= t.screen_count) continue;
            const IgdtScreen& scr = t.screens[sid];
            for (uint32_t k = 0; k < scr.touch_count; ++k) {
                const uint32_t pi = scr.touch_first + k;
                if (pi >= t.touch_path_count) break;
                HitWalk w = {0, 0, 0, 0, 0, 0, false};
                const IgdtNode* node = HitPath(w, t, rt, scr, t.touch_paths[pi], ev.x, ev.y);
                if (!node) continue;
                ts.captured = node;
                ts.cap_x0 = w.x0;
                ts.cap_y0 = w.y0;
                ts.cap_x1 = w.x1;
                ts.cap_y1 = w.y1;
                if (cb) cb(node->tag, ev, ctx);
                // STOCK-QUIRK: the screen is recorded after the callback.
                ts.captured_screen = sid;
                hit = true;
                break;
            }
        }
    }
}
