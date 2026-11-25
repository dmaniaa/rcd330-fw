// Stock source: display/controls/controlbase/vvisibilitybase.cpp
#include "display/controls/controlbase/vvisibilitybase.h"

#include "display/controls/controlbase/vmoveablebase.h"
#include "display/igdt/extend/igdt_wrapper.h"

namespace STDPlusLogicalControlLibrary {

// 0x002f9d48 VVisibilityBase::VVisibilityBase
VVisibilityBase::VVisibilityBase(uint32_t visibility_id, VMoveableBase* moveable)
    : m_visible(true), m_visibility_id(visibility_id), m_moveable(moveable) {}

// 0x002f996c VVisibilityBase::~VVisibilityBase
VVisibilityBase::~VVisibilityBase() {}

// STOCK-QUIRK: a hidden control without a move id still calls set_x(0, ...),
// which the wrapper rejects.
// 0x002f99bc VVisibilityBase::SetVisibility
void VVisibilityBase::SetVisibility(bool visible) {
    m_visible = visible;
    if (m_visibility_id != 0) {
        hmi_wrapper_igdt_set_branch(m_visibility_id, visible ? 1 : 0);
        return;
    }
    if (!m_moveable) return;
    if (!visible) {
        hmi_wrapper_igdt_set_x(m_moveable->GetMoveId(), -10000);
    } else {
        m_moveable->SetX(m_moveable->GetRelaX());
    }
}

// 0x002f9bb8 VVisibilityBase::IsVisible
bool VVisibilityBase::IsVisible() const {
    return m_visible;
}

// 0x002f9c40 VVisibilityBase::GetVisibilityId
uint32_t VVisibilityBase::GetVisibilityId() const {
    return m_visibility_id;
}

// 0x002f9cc4 VVisibilityBase::SetVisibilityId
void VVisibilityBase::SetVisibilityId(uint32_t id) {
    m_visibility_id = id;
}

// 0x002f9ab0 VVisibilityBase::GetMoveablePtr
VMoveableBase* VVisibilityBase::GetMoveablePtr() const {
    return m_moveable;
}

// 0x002f9b34 VVisibilityBase::SetMoveablePtr
void VVisibilityBase::SetMoveablePtr(VMoveableBase* moveable) {
    m_moveable = moveable;
}

} // namespace STDPlusLogicalControlLibrary
