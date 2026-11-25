// Stock source: display/controls/controlparts/compositeitem.cpp
//
// STDPlusLogicalControlLibrary::CompositeItem: a stretchable image made of
// 1, 3 (left/centre/right or upper/middle/lower) or 9 parts. SetSize keeps
// the corner/edge parts at their size and stretches the middle ones
// (ComposeCompositeParts in display/base/control_layout).
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_COMPOSITEITEM_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_COMPOSITEITEM_H

#include <stdint.h>

#include "display/base/control_layout.h"
#include "display/controls/controlbase/itembase.h"

namespace STDPlusLogicalControlLibrary {

class CompositeItem : public ItemBase {
    Q_OBJECT
public:
    // Stock enum CompositeItem::ITEM_COMBINATION_FORM (values of CompositeForm).
    typedef int ITEM_COMBINATION_FORM;

    // tag: the scene node the parts came from; the parts are re-parented to
    // this item, then SetSize(w, h) lays them out.
    CompositeItem(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t visibility_id,
                  ITEM_COMBINATION_FORM form, const ItemContainer& items, uint32_t move_id);
    virtual ~CompositeItem();

    virtual void SetSize(int32_t w, int32_t h);
    const ItemContainer& GetItems() const { return m_items; }
    ITEM_COMBINATION_FORM GetForm() const { return m_form; }
    uint32_t GetTag() const { return m_tag; }

private:
    uint32_t m_tag;
    ITEM_COMBINATION_FORM m_form;   // +0x44
    ItemContainer m_items;          // +0x48
};

} // namespace STDPlusLogicalControlLibrary

#endif
