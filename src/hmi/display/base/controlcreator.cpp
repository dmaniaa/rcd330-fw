// Stock source: display/base/controlcreator.cpp
#include "display/base/controlcreator.h"

#include "display/controls/controlbase/complexcontrol.h"
#include "display/controls/controlparts/compositebackground.h"
#include "display/controls/controlparts/compositeitem.h"
#include "display/controls/controlparts/igdtbutton.h"
#include "display/controls/controlparts/igdtimageitem.h"
#include "display/controls/controlparts/igdttextitem.h"
#include "display/igdt/igdt_tables.h"
#include "service/osal/osal_api.h"

using namespace STDPlusLogicalControlLibrary;

#define CREATOR_LOG(fmt, ...)                                                                         \
    do {                                                                                              \
        log_simple("ERROR", fmt, __VA_ARGS__);                                                        \
        printlog(3, "ERROR", "display/base/controlcreator.cpp", __FUNCTION__, __LINE__, fmt, __VA_ARGS__); \
    } while (0)

namespace {

const char kNullFmt[] = " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)";

void RectToXYWH(const IgdtElementRect& r, int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
    *x = r.left;
    *y = r.top;
    *w = r.right - r.left + 1;
    *h = r.bottom - r.top + 1;
}

IgdtImageItem* NewImageItem(const IgdtImagePart& p) {
    int32_t x, y, w, h;
    RectToXYWH(p.rect, &x, &y, &w, &h);
    if (p.dyn_id == 0 && p.tag_count == 1) return new IgdtImageItem(x, y, w, h, p.tags[0], p.move_id);
    return new IgdtImageItem(x, y, w, h, p.dyn_id, p.tags, p.tag_count, p.move_id);
}

IgdtTextItem* NewTextItem(const IgdtTextPart& p) {
    int32_t x, y, w, h;
    RectToXYWH(p.rect, &x, &y, &w, &h);
    return new IgdtTextItem(x, y, w, h, p.text_id, p.font, p.glow[0], p.glow[1], p.glow[2], p.dyn_tag, p.move_id);
}

// The composite's image items (CreateImage for each part).
bool BuildParts(const IgdtNode* root, ItemContainer* items) {
    IgdtImagePart parts[IGDT_COMPOSITE_MAX_PARTS];
    const int n = IgdtCollectCompositeParts(g_igdtTablesVw, root, parts, IGDT_COMPOSITE_MAX_PARTS);
    for (int i = 0; i < n; ++i) items->push_back(NewImageItem(parts[i]));
    return n > 0;
}

uint32_t DynamicOrZero(uint32_t tag) {
    return (tag & IGDT_TAG_DYNAMIC) ? tag : 0;
}

} // namespace

// No origin tag: GetElementRoot is IgdtFindNodeByTag (control_layout.cpp).
const IgdtNode* ControlCreator::GetElementRoot(uint8_t screen_id, uint32_t tag) {
    return IgdtFindNodeByTag(g_igdtTablesVw, screen_id, tag);
}

// No origin tag: GetElementRect is IgdtGetElementRect (control_layout.cpp).
bool ControlCreator::GetElementRect(const IgdtNode* node, int* x, int* y, int* w, int* h) {
    return IgdtCalcNodeRect(g_igdtTablesVw, node, x, y, w, h);
}

// 0x002d6f04 ControlCreator::CreateImage
IgdtImageItem* ControlCreator::CreateImage(const IgdtNode* node, uint32_t move_id) {
    IgdtImagePart p;
    if (!IgdtResolveImagePart(g_igdtTablesVw, node, move_id, &p)) return 0;
    return NewImageItem(p);
}

// 0x002d6c30 ControlCreator::CreateText
IgdtTextItem* ControlCreator::CreateText(const IgdtNode* node, uint32_t move_id) {
    IgdtTextPart p;
    if (!IgdtResolveTextPart(g_igdtTablesVw, node, move_id, &p)) return 0;
    return NewTextItem(p);
}

// 0x002dbd04 ControlCreator::CreateComposite
CompositeItem* ControlCreator::CreateComposite(const IgdtNode* root, int32_t x, int32_t y, int32_t w, int32_t h,
                                               int form) {
    if (!root) {
        CREATOR_LOG(kNullFmt, "pRoot", "CreateComposite", __LINE__);
        return 0;
    }
    ItemContainer items;
    if (!BuildParts(root, &items)) return 0;
    return new CompositeItem(x, y, w, h, root->tag, 0, form, items, DynamicOrZero(root->tag));
}

// 0x002dbf64 ControlCreator::CreateCompositeBackground
CompositeBackground* ControlCreator::CreateCompositeBackground(const IgdtNode* root, int32_t x, int32_t y, int32_t w,
                                                               int32_t h, int form) {
    if (!root) return 0;
    ItemContainer items;
    if (!BuildParts(root, &items)) return 0;
    return new CompositeBackground(x, y, w, h, root->tag, 0, form, items, DynamicOrZero(root->tag));
}

// TODO(phase3): a background that is a branch of composites becomes stock's
// multi-variant background (ctor 0x002c84b8, SetSize 0x002c82c4); ours
// builds no background for it and logs.
// 0x002dd4e0 ControlCreator::CreateButton
IgdtButton* ControlCreator::CreateButton(const IgdtNode* root, int32_t x, int32_t y, int32_t w, int32_t h, int form) {
    IgdtButtonNodes nodes;
    if (!IgdtParseButtonNodes(g_igdtTablesVw, root, &nodes)) {
        CREATOR_LOG(" ==== HMI ==== ERROR: CreateButton: 0x%08x is not a button (%s ->: %d)", root ? root->tag : 0u,
                    "CreateButton", __LINE__);
        return 0;
    }
    CompositeBackground* bg = 0;
    if (nodes.background_is_branch) {
        CREATOR_LOG(" ==== HMI ==== ERROR: CreateButton: branch background 0x%08x not supported (%s ->: %d)",
                    nodes.background->tag, "CreateButton", __LINE__);
    } else {
        bg = CreateCompositeBackground(nodes.background, x, y, w, h, form);
    }
    IgdtTextPart parts[8];
    const int n = IgdtCollectButtonTexts(g_igdtTablesVw, nodes, parts, 8);
    TextItemContainer texts;
    for (int i = 0; i < n; ++i) texts.push_back(NewTextItem(parts[i]));
    return new IgdtButton(x, y, w, h, root->tag, nodes.visibility_id, nodes.touch_id, nodes.dyn_id, bg, texts, 0,
                          DynamicOrZero(root->tag));
}

// 0x002d81f4 ControlCreator::CreateComposite
CompositeItem* ControlCreator::CreateComposite(ComplexControl* parent, uint8_t screen_id, uint32_t tag, int32_t x,
                                               int32_t y, int32_t w, int32_t h, int form) {
    return ParentAddItem(parent, CreateComposite(GetElementRoot(screen_id, tag), x, y, w, h, form));
}

// 0x002d8f10 ControlCreator::CreateButton
IgdtButton* ControlCreator::CreateButton(ComplexControl* parent, uint8_t screen_id, uint32_t tag, int32_t x,
                                         int32_t y, int32_t w, int32_t h, int form) {
    return ParentAddItem(parent, CreateButton(GetElementRoot(screen_id, tag), x, y, w, h, form));
}

// 0x002db0ac ControlCreator::ParentAddItem
CompositeItem* ControlCreator::ParentAddItem(ComplexControl* parent, CompositeItem* item) {
    if (!item) {
        CREATOR_LOG(kNullFmt, "pControl", "ParentAddItem", 0xd6);
    } else if (parent) {
        parent->AddChild(item);
    }
    return item;
}

// 0x002db2fc ControlCreator::ParentAddItem
IgdtButton* ControlCreator::ParentAddItem(ComplexControl* parent, IgdtButton* item) {
    if (!item) {
        CREATOR_LOG(kNullFmt, "pControl", "ParentAddItem", 0xd6);
    } else if (parent) {
        parent->AddChild(item);
    }
    return item;
}
