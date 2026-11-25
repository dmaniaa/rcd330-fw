// Stock source: display/base/controlcreator.cpp
//
// ControlCreator builds controls from the IGDT scene tree of a screen:
// a page passes the screen id, the root node tag and the rect stock
// hard-codes for it, e.g. vAppNoConnection (0x0033a660):
//
//   ControlCreator::CreateComposite(this, 4, 0x40100164, 0, 0x182, 800, 0x5e, ICF_9_PARTS);
//   IgdtButton* info = ControlCreator::CreateButton(this, 4, 0x40100166, 0, 0x18c, 800, 0x54, ICF_9_PARTS);
//
// The (parent, ...) forms add the new control to the parent
// (ComplexControl::AddChild), which binds its signals to the parent's On*
// slots. The node walks are in display/base/control_layout.
#ifndef RCD330_HMI_DISPLAY_BASE_CONTROLCREATOR_H
#define RCD330_HMI_DISPLAY_BASE_CONTROLCREATOR_H

#include <stdint.h>

#include "display/base/control_layout.h"

namespace STDPlusLogicalControlLibrary {
class ComplexControl;
class CompositeBackground;
class CompositeItem;
class IgdtButton;
class IgdtImageItem;
class IgdtTextItem;
class ItemBase;
} // namespace STDPlusLogicalControlLibrary

class ControlCreator {
public:
    typedef STDPlusLogicalControlLibrary::ComplexControl ComplexControl;
    typedef STDPlusLogicalControlLibrary::CompositeBackground CompositeBackground;
    typedef STDPlusLogicalControlLibrary::CompositeItem CompositeItem;
    typedef STDPlusLogicalControlLibrary::IgdtButton IgdtButton;
    typedef STDPlusLogicalControlLibrary::IgdtImageItem IgdtImageItem;
    typedef STDPlusLogicalControlLibrary::IgdtTextItem IgdtTextItem;
    typedef STDPlusLogicalControlLibrary::ItemBase ItemBase;

    // Scene lookup (IgdtFindNodeByTag / IgdtCalcNodeRect on the VW tables).
    static const IgdtNode* GetElementRoot(uint8_t screen_id, uint32_t tag);
    static bool GetElementRect(const IgdtNode* node, int* x, int* y, int* w, int* h);

    // One image part (image node, or a state node's images); move_id is the
    // nearest dynamic group above it. NULL when the node is neither.
    static IgdtImageItem* CreateImage(const IgdtNode* node, uint32_t move_id);
    // One text item (text node or composite text with glow).
    static IgdtTextItem* CreateText(const IgdtNode* node, uint32_t move_id);
    // A CompositeItem / CompositeBackground of the root's image parts laid
    // out at (x, y, w, h) with `form` (CompositeForm). NULL when the root
    // yields no parts.
    static CompositeItem* CreateComposite(const IgdtNode* root, int32_t x, int32_t y, int32_t w, int32_t h, int form);
    static CompositeBackground* CreateCompositeBackground(const IgdtNode* root, int32_t x, int32_t y, int32_t w,
                                                          int32_t h, int form);
    // An IgdtButton from a button root (see IgdtParseButtonNodes); `form`
    // lays out its background. NULL when the root is not a button.
    static IgdtButton* CreateButton(const IgdtNode* root, int32_t x, int32_t y, int32_t w, int32_t h, int form);

    // Page forms: look up `tag` on screen_id, create, add to parent.
    static CompositeItem* CreateComposite(ComplexControl* parent, uint8_t screen_id, uint32_t tag, int32_t x,
                                          int32_t y, int32_t w, int32_t h, int form);
    static IgdtButton* CreateButton(ComplexControl* parent, uint8_t screen_id, uint32_t tag, int32_t x, int32_t y,
                                    int32_t w, int32_t h, int form);

    // parent->AddChild(item) (NULL parent: nothing); returns item.
    static CompositeItem* ParentAddItem(ComplexControl* parent, CompositeItem* item);
    static IgdtButton* ParentAddItem(ComplexControl* parent, IgdtButton* item);
};

#endif
