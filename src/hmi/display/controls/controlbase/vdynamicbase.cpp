// Stock source: display/controls/controlbase/vdynamicbase.cpp
#include "display/controls/controlbase/vdynamicbase.h"

#include "display/igdt/extend/igdt_wrapper.h"

namespace STDPlusLogicalControlLibrary {

// 0x002f848c VDynamicBase::VDynamicBase
VDynamicBase::VDynamicBase(uint32_t dyn_id, int dyn_type) : m_dyn_id(0), m_dyn_type(DYN_NONE), m_dyn_index(0) {
    SetDynType(dyn_type);
    SetDynId(dyn_id);
    SetDynIndex(0);
}

// 0x002f80bc VDynamicBase::~VDynamicBase
VDynamicBase::~VDynamicBase() {}

// 0x002f831c VDynamicBase::SetDynId
void VDynamicBase::SetDynId(uint32_t id) {
    if (m_dyn_type == DYN_NONE) {
        if ((id & 0x0f000000u) == 0x01000000u) {
            m_dyn_type = DYN_BRANCH;
        } else if ((id & 0x0f000000u) == 0x03000000u) {
            m_dyn_type = DYN_STATE;
        }
    }
    m_dyn_id = id;
}

// 0x002f8298 VDynamicBase::GetDynId
uint32_t VDynamicBase::GetDynId() const {
    return m_dyn_id;
}

// 0x002f8214 VDynamicBase::SetDynType
void VDynamicBase::SetDynType(int type) {
    m_dyn_type = type;
}

// 0x002f8190 VDynamicBase::GetDynType
int VDynamicBase::GetDynType() const {
    return m_dyn_type;
}

// STOCK-QUIRK: no range check; a dyn id of 0 reaches the wrapper (rejected there).
// 0x002f83d4 VDynamicBase::SetDynIndex
void VDynamicBase::SetDynIndex(int index) {
    m_dyn_index = index;
    if (m_dyn_type == DYN_BRANCH) {
        hmi_wrapper_igdt_set_branch(m_dyn_id, (uint16_t)index);
    } else if (m_dyn_type == DYN_STATE) {
        hmi_wrapper_igdt_set_state(m_dyn_id, (uint16_t)index);
    }
}

// 0x002f810c VDynamicBase::GetDynIndex
int VDynamicBase::GetDynIndex() const {
    return m_dyn_index;
}

} // namespace STDPlusLogicalControlLibrary
