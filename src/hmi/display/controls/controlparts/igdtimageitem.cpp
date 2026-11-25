// File name is ours; stock name unknown.
#include "display/controls/controlparts/igdtimageitem.h"

#include "display/igdt/extend/igdt_wrapper.h"

namespace STDPlusLogicalControlLibrary {

// 0x002ceb74 IgdtImageItem::IgdtImageItem
IgdtImageItem::IgdtImageItem(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t move_id)
    : ItemBase(x, y, w, h, 0, 0), m_dynamic(0, VDynamicBase::DYN_STATE), m_tags(1, tag) {
    SetMoveId(move_id);
}

// 0x002ceab8 IgdtImageItem::IgdtImageItem
IgdtImageItem::IgdtImageItem(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t dyn_id, const uint32_t* tags,
                             int count, uint32_t move_id)
    : ItemBase(x, y, w, h, 0, 0), m_dynamic(0, VDynamicBase::DYN_STATE) {
    m_dynamic.SetDynId(dyn_id);
    if (tags && count > 0) m_tags.assign(tags, tags + count);
    SetMoveId(move_id);
}

// 0x002cec38 IgdtImageItem::~IgdtImageItem
IgdtImageItem::~IgdtImageItem() {}

// 0x002cedcc IgdtImageItem::SetX
void IgdtImageItem::SetRelX(int32_t rel_x) {
    if (GetMoveId() != 0) {
        ItemBase::SetRelX(rel_x);
        return;
    }
    Moveable().SetRelaX(rel_x);
    const ItemBase* p = GetParentItem();
    const int32_t parent_disp = p ? p->GetDisplacementX() : 0;
    for (size_t i = 0; i < m_tags.size(); ++i) {
        hmi_wrapper_igdt_set_x(m_tags[i], (int16_t)(GetAbsX() - parent_disp));
    }
}

// 0x002ceeb8 IgdtImageItem::SetY
void IgdtImageItem::SetRelY(int32_t rel_y) {
    if (GetMoveId() != 0) {
        ItemBase::SetRelY(rel_y);
        return;
    }
    Moveable().SetRelaY(rel_y);
    const ItemBase* p = GetParentItem();
    const int32_t parent_disp = p ? p->GetDisplacementY() : 0;
    for (size_t i = 0; i < m_tags.size(); ++i) {
        hmi_wrapper_igdt_set_y(m_tags[i], (int16_t)(GetAbsY() - parent_disp));
    }
}

// 0x002cefa4 IgdtImageItem::SetSize
void IgdtImageItem::SetSize(int32_t w, int32_t h) {
    ItemBase::SetSize(w, h);
    if (GetOrigWidth() == 0 || GetOrigHeight() == 0) return;
    const float sx = (float)((double)w / (double)GetOrigWidth());
    const float sy = (float)((double)h / (double)GetOrigHeight());
    for (size_t i = 0; i < m_tags.size(); ++i) hmi_wrapper_igdt_set_scale(m_tags[i], sx, sy);
}

} // namespace STDPlusLogicalControlLibrary
