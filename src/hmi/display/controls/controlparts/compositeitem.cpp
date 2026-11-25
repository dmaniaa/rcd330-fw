// Stock source: display/controls/controlparts/compositeitem.cpp
#include "display/controls/controlparts/compositeitem.h"

#include "service/osal/osal_api.h"

namespace STDPlusLogicalControlLibrary {

// DEVIATION: stock asserts the part count for the form (abort); ours logs
// and leaves the parts where the scene put them (SetSize lays out nothing).
// 0x002c9a10 CompositeItem::CompositeItem
CompositeItem::CompositeItem(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t visibility_id,
                             ITEM_COMBINATION_FORM form, const ItemContainer& items, uint32_t move_id)
    : ItemBase(x, y, w, h, move_id, visibility_id), m_tag(tag), m_form(form), m_items(items) {
    if ((int)m_items.size() != CompositePartCount(form)) {
        log_simple("ERROR", " ==== HMI ==== ERROR: CompositeItem 0x%08x: %d parts for form %d", tag,
                   (int)m_items.size(), form);
    }
    for (ItemContainer::iterator it = m_items.begin(); it != m_items.end(); ++it) {
        if (*it) (*it)->SetParent(this);
    }
    SetSize(w, h);
}

// No origin tag: stock's destructor is not exported as a separate function.
CompositeItem::~CompositeItem() {}

// 0x002c98f8 CompositeItem::SetSize
void CompositeItem::SetSize(int32_t w, int32_t h) {
    ItemBase::SetSize(w, h);
    const int n = (int)m_items.size();
    if (n == 0 || n > IGDT_COMPOSITE_MAX_PARTS || n != CompositePartCount(m_form)) return;
    CompositePart parts[IGDT_COMPOSITE_MAX_PARTS];
    for (int i = 0; i < n; ++i) {
        if (!m_items[i]) return;
        parts[i].rel_x = m_items[i]->Moveable().GetRelaX();
        parts[i].rel_y = m_items[i]->Moveable().GetRelaY();
        parts[i].w = m_items[i]->GetWidth();
        parts[i].h = m_items[i]->GetHeight();
        parts[i].x_set = false;
        parts[i].y_set = false;
    }
    if (!ComposeCompositeParts(m_form, w, h, parts, n)) return;
    for (int i = 0; i < n; ++i) {
        if (parts[i].x_set) m_items[i]->SetRelX(parts[i].rel_x);
        if (parts[i].y_set) m_items[i]->SetRelY(parts[i].rel_y);
        m_items[i]->SetSize(parts[i].w, parts[i].h);
    }
}

} // namespace STDPlusLogicalControlLibrary
