// File name is ours; stock name unknown.
#include "display/base/control_layout.h"

#include "display/igdt/igdt_touch.h"

namespace {

const uint32_t kKindShift = 24;

uint32_t KindOf(uint32_t tag) {
    return (tag & IGDT_TAG_KIND_MASK) >> kKindShift;
}

bool IsDynamicTag(uint32_t tag) {
    return (tag & IGDT_TAG_DYNAMIC) != 0;
}

const IgdtNode* Child(const IgdtSkinTables& t, const IgdtNode* n, int i) {
    if (!n || n->child_first < 0 || i < 0 || i >= n->child_count) return 0;
    const uint32_t idx = (uint32_t)n->child_first + (uint32_t)i;
    return idx < t.node_count ? &t.nodes[idx] : 0;
}

IgdtElementRect NullRect() {
    IgdtElementRect r = {0, 0, -1, -1};
    return r;
}

bool IsNullRect(const IgdtElementRect& r) {
    return r.left == 0 && r.right == -1 && r.top == 0 && r.bottom == -1;
}

// DEVIATION: stock indexes the record tables without bounds checks; ours
// returns NULL past the end so a bad tag yields the null rect.
const IgdtImgRecord* ImgRecord(const IgdtSkinTables& t, uint32_t tag) {
    const uint32_t i = tag & IGDT_TAG_INDEX_MASK;
    if (IsDynamicTag(tag)) return i < IGDT_DYN_IMG_COUNT ? &t.dyn_imgs[i] : 0;
    return i < t.static_img_count ? &t.static_imgs[i] : 0;
}

const IgdtTxtRecord* TxtRecord(const IgdtSkinTables& t, uint32_t tag) {
    const uint32_t i = tag & IGDT_TAG_INDEX_MASK;
    if (IsDynamicTag(tag)) return i < IGDT_DYN_TXT_COUNT ? &t.dyn_txts[i] : 0;
    return i < t.static_txt_count ? &t.static_txts[i] : 0;
}

const IgdtTextLayout* Layout(const IgdtSkinTables& t, uint16_t eidx) {
    return eidx < t.text_layout_count ? &t.text_layouts[eidx] : 0;
}

// 0x002d65e0 FindElement
// Name is ours: the depth-first tag search under one root node.
const IgdtNode* FindElement(const IgdtSkinTables& t, const IgdtNode* n, uint32_t tag) {
    if (!n) return 0;
    if (n->tag == tag) return n;
    for (int i = 0; i < n->child_count; ++i) {
        const IgdtNode* hit = FindElement(t, Child(t, n, i), tag);
        if (hit) return hit;
    }
    return 0;
}

// Layout-aligned left edge of a text node: align 1 (right) -> x - width,
// align 2 (centre) -> x - width / 2, else x.
// 0x002d6720 ControlCreator::GetTextCoordX
int GetTextCoordX(const IgdtSkinTables& t, const IgdtNode* n) {
    const IgdtTxtRecord* rec = n ? TxtRecord(t, n->tag) : 0;
    const IgdtTextLayout* lay = n ? Layout(t, n->eidx) : 0;
    if (!rec || !lay) return 0;
    switch (lay->align & 3) {
    case 1:
        return (int16_t)(rec->x - lay->width);
    case 2:
        return (int16_t)(rec->x - (lay->width >> 1));
    default:
        return rec->x;
    }
}

// QRect::operator| for two valid rects.
IgdtElementRect Unite(const IgdtElementRect& a, const IgdtElementRect& b) {
    IgdtElementRect r;
    r.left = a.left < b.left ? a.left : b.left;
    r.top = a.top < b.top ? a.top : b.top;
    r.right = a.right > b.right ? a.right : b.right;
    r.bottom = a.bottom > b.bottom ? a.bottom : b.bottom;
    return r;
}

// 0x002c9168 CompositeItem::ComposeOfLCR
// Left keeps its width at height h, the centre starts after it and takes
// what is left of w, the right part is moved to the far edge.
void ComposeOfLCR(int w, int h, CompositePart& l, CompositePart& c, CompositePart& r) {
    l.h = h;
    c.rel_x = l.rel_x + l.w;
    c.x_set = true;
    c.w = w - l.w - r.w;
    c.h = h;
    r.rel_x = l.rel_x + (w - r.w);
    r.x_set = true;
    r.h = h;
}

// 0x002c9340 CompositeItem::ComposeOfUML
void ComposeOfUML(int w, int h, CompositePart& u, CompositePart& m, CompositePart& l) {
    u.w = w;
    m.rel_y = u.rel_y + u.h;
    m.y_set = true;
    m.w = w;
    m.h = h - u.h - l.h;
    l.rel_y = u.rel_y + (h - l.h);
    l.y_set = true;
    l.w = w;
}

bool AppendTag(IgdtImagePart* out, uint32_t tag) {
    if (out->tag_count >= IGDT_IMAGE_MAX_TAGS) return false;
    out->tags[out->tag_count++] = tag;
    return true;
}

// Absolute x/y the stock image item writes for a part (IgdtImageItem
// SetX 0x002cedcc / SetY 0x002ceeb8): the move-id group gets its displacement relative to the
// parent's, plain tags their absolute position minus the parent's
// displacement. The parent (the composite) has displacement 0 here.
int ApplyPartAxis(IgdtRuntime& rt, const IgdtImagePart& p, int axis, int abs) {
    const int orig = axis ? p.rect.top : p.rect.left;
    if (p.move_id != 0) {
        const int16_t v = (int16_t)(abs - orig);
        return axis ? IgdtSetPosY(rt, p.move_id, v) : IgdtSetPosX(rt, p.move_id, v);
    }
    for (int i = 0; i < p.tag_count; ++i) {
        const int ret = axis ? IgdtSetPosY(rt, p.tags[i], (int16_t)abs) : IgdtSetPosX(rt, p.tags[i], (int16_t)abs);
        if (ret != IGDT_OK) return ret;
    }
    return IGDT_OK;
}

} // namespace

// GFX_MGR02 screens are indexed by id; stock reads scr_table[screen_id].
// 0x002d6884 ControlCreator::GetElementRoot
const IgdtNode* IgdtFindNodeByTag(const IgdtSkinTables& t, uint8_t screen_id, uint32_t tag) {
    if (screen_id >= IGDT_CONTROL_SCREEN_LIMIT || screen_id >= t.screen_count) return 0;
    const IgdtScreen& s = t.screens[screen_id];
    for (uint32_t i = 0; i < s.root_count; ++i) {
        const uint32_t idx = s.root_first + i;
        if (idx >= t.node_count) break;
        const IgdtNode* hit = FindElement(t, &t.nodes[idx], tag);
        if (hit) return hit;
    }
    return 0;
}

// 0x002d690c ControlCreator::GetElementRect
IgdtElementRect IgdtGetElementRect(const IgdtSkinTables& t, const IgdtNode* node) {
    IgdtElementRect r = NullRect();
    if (!node) return r;
    const uint32_t kind = KindOf(node->tag);
    if (kind == IGDT_KIND_IMG) {
        const IgdtImgRecord* rec = ImgRecord(t, node->tag);
        if (!rec || node->eidx >= t.tex_count) return r;
        const IgdtTexEntry& tex = t.textures[node->eidx];
        r.left = rec->x;
        r.top = rec->y;
        r.right = rec->x + tex.w - 1;
        r.bottom = rec->y + tex.h - 1;
        return r;
    }
    if (kind == IGDT_KIND_TXT || kind == IGDT_KIND_TXT_DYN) {
        const IgdtTxtRecord* rec = TxtRecord(t, node->tag);
        const IgdtTextLayout* lay = Layout(t, node->eidx);
        if (!rec || !lay) return r;
        r.left = GetTextCoordX(t, node);
        r.top = rec->y;
        r.right = r.left + lay->width - 1;
        // STOCK-QUIRK: a text rect has no height (bottom = top - 1), so
        // group unions skip text nodes.
        r.bottom = rec->y - 1;
        return r;
    }
    if (kind == IGDT_KIND_GROUP) {
        const IgdtGrpFlags* f = node->eidx < t.grp_flag_count ? &t.grp_flags[node->eidx] : 0;
        if (f && (f->flags & 3) != 0) {
            if (f->clip_idx >= t.clip_count) return r;
            const IgdtClipRect& c = t.clips[f->clip_idx];
            r.left = c.x;
            r.top = c.y;
            r.right = c.x + c.w - 1;
            r.bottom = c.y + c.h - 1;
            return r;
        }
    } else if (kind != IGDT_KIND_STATE && kind != IGDT_KIND_BRANCH) {
        return r;
    }
    for (int i = 0; i < node->child_count; ++i) {
        const IgdtElementRect c = IgdtGetElementRect(t, Child(t, node, i));
        if (c.left > c.right || c.top > c.bottom || IsNullRect(c)) continue;
        r = IsNullRect(r) ? c : Unite(c, r);
    }
    return r;
}

// Name is ours: IgdtGetElementRect as x, y, w, h.
bool IgdtCalcNodeRect(const IgdtSkinTables& t, const IgdtNode* node, int* x, int* y, int* w, int* h) {
    if (!node || !x || !y || !w || !h) return false;
    const IgdtElementRect r = IgdtGetElementRect(t, node);
    if (IsNullRect(r)) return false;
    *x = r.left;
    *y = r.top;
    *w = r.right - r.left + 1;
    *h = r.bottom - r.top + 1;
    return true;
}

// Name is ours.
int CompositePartCount(int form) {
    switch (form) {
    case ICF_ONE_PART:
        return 1;
    case ICF_LCR:
    case ICF_UML:
        return 3;
    case ICF_9_PARTS:
        return 9;
    default:
        return 0;
    }
}

// Name is ours: the switch of CompositeItem::SetSize (0x002c98f8 in
// compositeitem.cpp) on the portable part list.
bool ComposeCompositeParts(int form, int w, int h, CompositePart* parts, int count) {
    if (!parts || count <= 0 || count != CompositePartCount(form)) return false;
    switch (form) {
    case ICF_ONE_PART:
        parts[0].w = w;
        parts[0].h = h;
        return true;
    case ICF_LCR:
        ComposeOfLCR(w, h, parts[0], parts[1], parts[2]);
        return true;
    case ICF_UML:
        ComposeOfUML(w, h, parts[0], parts[1], parts[2]);
        return true;
    default:
        break;
    }
    // ICF_9_PARTS (ComposeOf9Parts): three LCR rows of heights UL.h,
    // h - UL.h - LL.h and LL.h, then the lower two rows moved down.
    const int upper_h = parts[0].h;
    const int lower_h = parts[6].h;
    ComposeOfLCR(w, upper_h, parts[0], parts[1], parts[2]);
    ComposeOfLCR(w, h - upper_h - lower_h, parts[3], parts[4], parts[5]);
    ComposeOfLCR(w, lower_h, parts[6], parts[7], parts[8]);
    const int mid_y = parts[0].rel_y + parts[0].h;
    const int low_y = parts[0].rel_y + (h - parts[6].h);
    for (int i = 3; i < 6; ++i) {
        parts[i].rel_y = mid_y;
        parts[i].y_set = true;
    }
    for (int i = 6; i < 9; ++i) {
        parts[i].rel_y = low_y;
        parts[i].y_set = true;
    }
    return true;
}

// 0x002d6590 GetDynId
// Name is ours.
uint32_t IgdtNodeDynId(const IgdtNode* node) {
    if (!node) return 0;
    if (IsDynamicTag(node->tag)) return (uint32_t)node->eidx | (node->tag & IGDT_TAG_KIND_MASK);
    return node->tag;
}

// Name is ours: the node walk of ControlCreator::CreateImage (0x002d6f04).
bool IgdtResolveImagePart(const IgdtSkinTables& t, const IgdtNode* node, uint32_t move_id, IgdtImagePart* out) {
    if (!out) return false;
    while (node) {
        if (!IsDynamicTag(move_id)) move_id = 0;
        const uint32_t kind = KindOf(node->tag);
        if (kind == IGDT_KIND_IMG) {
            out->tag_count = 0;
            AppendTag(out, node->tag);
            out->rect = IgdtGetElementRect(t, node);
            out->move_id = move_id;
            out->dyn_id = 0;
            return true;
        }
        if (kind == IGDT_KIND_STATE) {
            if (node->child_count == 0) return false;
            out->tag_count = 0;
            for (int i = 0; i < node->child_count; ++i) {
                const IgdtNode* c = Child(t, node, i);
                if (!c || !AppendTag(out, c->tag)) return false;
            }
            out->rect = IgdtGetElementRect(t, node);
            out->move_id = move_id;
            out->dyn_id = (uint32_t)node->eidx | (IGDT_KIND_STATE << kKindShift);
            return true;
        }
        if (kind != IGDT_KIND_GROUP && kind != IGDT_KIND_BRANCH) return false;
        if (move_id == 0) move_id = node->tag;
        node = Child(t, node, 0);
    }
    return false;
}

// Name is ours: the part list of ControlCreator::CreateComposite (0x002dbd04).
int IgdtCollectCompositeParts(const IgdtSkinTables& t, const IgdtNode* root, IgdtImagePart* out, int max) {
    if (!root || !out || max <= 0) return 0;
    const uint32_t kind = KindOf(root->tag);
    if (kind == IGDT_KIND_IMG || kind == IGDT_KIND_STATE) {
        return IgdtResolveImagePart(t, root, 0, &out[0]) ? 1 : 0;
    }
    if (kind != IGDT_KIND_GROUP) return 0;
    int n = 0;
    for (int i = 0; i < root->child_count && n < max; ++i) {
        if (IgdtResolveImagePart(t, Child(t, root, i), 0, &out[n])) ++n;
    }
    return n;
}

// Name is ours: CreateComposite + CompositeItem::SetSize on the runtime.
bool IgdtApplyCompositeSize(const IgdtSkinTables& t, IgdtRuntime& rt, const IgdtNode* root, int x, int y, int w,
                            int h, int slice_mode) {
    IgdtImagePart parts[IGDT_COMPOSITE_MAX_PARTS];
    const int n = IgdtCollectCompositeParts(t, root, parts, IGDT_COMPOSITE_MAX_PARTS);
    if (n == 0 || n != CompositePartCount(slice_mode)) return false;
    CompositePart layout[IGDT_COMPOSITE_MAX_PARTS];
    int orig_w[IGDT_COMPOSITE_MAX_PARTS];
    int orig_h[IGDT_COMPOSITE_MAX_PARTS];
    for (int i = 0; i < n; ++i) {
        // ItemBase::SetParent: relative position = own absolute - composite's.
        layout[i].rel_x = parts[i].rect.left - x;
        layout[i].rel_y = parts[i].rect.top - y;
        orig_w[i] = layout[i].w = parts[i].rect.right - parts[i].rect.left + 1;
        orig_h[i] = layout[i].h = parts[i].rect.bottom - parts[i].rect.top + 1;
        layout[i].x_set = false;
        layout[i].y_set = false;
    }
    if (!ComposeCompositeParts(slice_mode, w, h, layout, n)) return false;
    bool ok = true;
    for (int i = 0; i < n; ++i) {
        if (layout[i].x_set) ok &= ApplyPartAxis(rt, parts[i], 0, x + layout[i].rel_x) == IGDT_OK;
        if (layout[i].y_set) ok &= ApplyPartAxis(rt, parts[i], 1, y + layout[i].rel_y) == IGDT_OK;
        // IgdtImageItem::SetSize (0x002cefa4): skipped for a zero original size.
        if (orig_w[i] == 0 || orig_h[i] == 0) continue;
        const float sx = (float)((double)layout[i].w / (double)orig_w[i]);
        const float sy = (float)((double)layout[i].h / (double)orig_h[i]);
        for (int k = 0; k < parts[i].tag_count; ++k) ok &= IgdtSetScale(rt, parts[i].tags[k], sx, sy) == IGDT_OK;
    }
    return ok;
}

// Name is ours: the node walk of ControlCreator::CreateButton (0x002dd4e0).
bool IgdtParseButtonNodes(const IgdtSkinTables& t, const IgdtNode* root, IgdtButtonNodes* out) {
    if (!root || !out || root->child_count == 0) return false;
    out->visibility_id = 0;
    out->touch_id = 0;
    out->dyn_id = 0;
    out->content = 0;
    out->background = 0;
    out->background_is_branch = false;
    out->text_root = 0;

    const IgdtNode* first = Child(t, root, 0);
    if (!first) return false;
    const IgdtNode* content = root;
    if (KindOf(first->tag) == IGDT_KIND_BRANCH) {
        // Visibility branch: the content is its first child with children.
        if (first->child_count < 2) return false;
        out->visibility_id = IgdtNodeDynId(first);
        content = 0;
        for (int i = 0; i < first->child_count; ++i) {
            const IgdtNode* c = Child(t, first, i);
            if (c && c->child_count != 0) {
                content = c;
                break;
            }
        }
        if (!content) return false;
    }
    if (content->child_count < 2) return false;
    out->content = content;
    const IgdtNode* touch = Child(t, content, 0);
    const IgdtNode* bg = Child(t, content, 1);
    if (!touch || !bg) return false;
    out->touch_id = touch->tag;
    out->background = bg;
    out->background_is_branch = KindOf(bg->tag) == IGDT_KIND_BRANCH && bg->child_count != 0;
    if (content->child_count == 2) return true;

    const IgdtNode* third = Child(t, content, 2);
    if (!third) return true;
    const uint32_t kind = KindOf(third->tag);
    if (kind == IGDT_KIND_BRANCH || kind == IGDT_KIND_STATE) {
        out->dyn_id = IgdtNodeDynId(third);
        if (content->child_count == 3) return true;
        out->text_root = Child(t, content, 3);
    } else {
        out->text_root = third;
    }
    return true;
}

// Name is ours: the node walk of ControlCreator::CreateText (0x002d6c30).
bool IgdtResolveTextPart(const IgdtSkinTables& t, const IgdtNode* node, uint32_t move_id, IgdtTextPart* out) {
    if (!out) return false;
    while (node) {
        if (!IsDynamicTag(move_id)) move_id = 0;
        const uint32_t kind = KindOf(node->tag);
        if (kind == IGDT_KIND_TXT || kind == IGDT_KIND_TXT_DYN) {
            const IgdtTextLayout* lay = Layout(t, node->eidx);
            out->rect = IgdtGetElementRect(t, node);
            out->text_id = node->eidx;
            out->font = lay ? lay->font : 0;
            out->glow[0] = out->glow[1] = out->glow[2] = 0;
            out->dyn_tag = IsDynamicTag(node->tag) ? node->tag : 0;
            out->move_id = move_id;
            return true;
        }
        if (kind != IGDT_KIND_GROUP && kind != IGDT_KIND_BRANCH) return false;
        if (node->child_count != 1) break;
        if (move_id == 0) move_id = node->tag;
        node = Child(t, node, 0);
    }
    if (!node) return false;
    // Composite text: child 0 holds the three glow images, child 1 is the text.
    const IgdtNode* glow = Child(t, node, 0);
    const IgdtNode* text = Child(t, node, 1);
    if (!glow || !text || glow->child_count < 3) return false;
    const IgdtTextLayout* lay = Layout(t, text->eidx);
    out->rect = IgdtGetElementRect(t, node);
    out->text_id = text->eidx;
    out->font = lay ? lay->font : 0;
    for (int i = 0; i < 3; ++i) {
        const IgdtNode* g = Child(t, glow, i);
        out->glow[i] = g ? g->tag : 0;
    }
    out->dyn_tag = IsDynamicTag(text->tag) ? text->tag : 0;
    if (move_id == 0 && IsDynamicTag(node->tag)) move_id = node->tag;
    out->move_id = move_id;
    return true;
}

// Name is ours: the text-item part of ControlCreator::CreateButton.
int IgdtCollectButtonTexts(const IgdtSkinTables& t, const IgdtButtonNodes& nodes, IgdtTextPart* out, int max) {
    const IgdtNode* root = nodes.text_root;
    if (!root || !out || max <= 0) return 0;
    if (root->child_count == 0) return IgdtResolveTextPart(t, root, 0, &out[0]) ? 1 : 0;
    const IgdtNode* first = Child(t, root, 0);
    const IgdtNode* second = Child(t, root, 1);
    if (first && KindOf(first->tag) == IGDT_KIND_GROUP && second &&
        (KindOf(second->tag) == IGDT_KIND_TXT || KindOf(second->tag) == IGDT_KIND_TXT_DYN)) {
        // A composite text directly under the text root.
        return IgdtResolveTextPart(t, root, root->tag, &out[0]) ? 1 : 0;
    }
    int n = 0;
    for (int i = 0; i < root->child_count && n < max; ++i) {
        const IgdtNode* c = Child(t, root, i);
        if (!c) continue;
        // A non-group child moves with the text root, a group child by itself.
        const uint32_t move = KindOf(c->tag) != IGDT_KIND_GROUP ? root->tag : c->tag;
        if (IgdtResolveTextPart(t, c, move, &out[n])) ++n;
    }
    return n;
}

// Name is ours: IgdtButtonBase::TouchEventHandler (0x002ea27c) with the
// ButtonBase handlers it reaches (OnLeave 0x002c73f0, OnPress 0x002c7288,
// OnRelease 0x002c7320, OnClicked 0x002c7398), on state values only.
ButtonClickResult EvalButtonTouch(int cur_state, uint32_t event_type) {
    ButtonClickResult r;
    r.state_changed = false;
    r.new_state = cur_state;
    r.clicked = false;
    // ButtonBase IsDisabled (0x002c6e08): GetState() == 2, checked by each handler.
    if ((event_type & IGDT_TOUCH_CANCEL_OUTSIDE) != 0 && r.new_state != BTN_DISABLED) {
        r.new_state = BTN_NORMAL;   // OnLeave
        r.state_changed = true;
    }
    switch (event_type & 0xff) {
    case IGDT_TOUCH_PRESS:
        if (r.new_state != BTN_DISABLED) {
            r.new_state = BTN_PRESSED;
            r.state_changed = true;
        }
        break;
    case IGDT_TOUCH_RELEASE:
        if (r.new_state != BTN_DISABLED) {
            r.new_state = BTN_NORMAL;
            r.state_changed = true;
        }
        // STOCK-QUIRK: the drag-out test compares the whole word
        // with 0x80000003 rather than testing the bit.
        if (event_type != (IGDT_TOUCH_CANCEL_OUTSIDE | IGDT_TOUCH_RELEASE) &&
            (event_type & IGDT_TOUCH_CANCEL_PAGE) == 0 && r.new_state != BTN_DISABLED) {
            r.clicked = true;
        }
        break;
    default:
        break;   // 0 move (OnMove), anything else ignored
    }
    return r;
}
