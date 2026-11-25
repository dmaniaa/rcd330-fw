// Stock source: display/controls/controlbase/vmoveablebase.cpp
#include "display/controls/controlbase/vmoveablebase.h"

#include "display/igdt/extend/igdt_wrapper.h"

namespace STDPlusLogicalControlLibrary {

// 0x002f8b4c VMoveableBase::VMoveableBase
VMoveableBase::VMoveableBase(int32_t rel_x, int32_t rel_y, uint32_t move_id)
    : m_move_id(0), m_rel_x(rel_x), m_rel_y(rel_y) {
    SetMoveId(move_id);
}

// 0x002f862c VMoveableBase::~VMoveableBase
VMoveableBase::~VMoveableBase() {}

// 0x002f8910 VMoveableBase::SetMoveId
void VMoveableBase::SetMoveId(uint32_t move_id) {
    m_move_id = move_id;
}

// 0x002f888c VMoveableBase::GetMoveId
uint32_t VMoveableBase::GetMoveId() const {
    return m_move_id;
}

// 0x002f8700 VMoveableBase::SetRelaX
void VMoveableBase::SetRelaX(int32_t x) {
    m_rel_x = x;
}

// 0x002f867c VMoveableBase::SetRelaY
void VMoveableBase::SetRelaY(int32_t y) {
    m_rel_y = y;
}

// 0x002f8808 VMoveableBase::GetRelaX
int32_t VMoveableBase::GetRelaX() const {
    return m_rel_x;
}

// 0x002f8784 VMoveableBase::GetRelaY
int32_t VMoveableBase::GetRelaY() const {
    return m_rel_y;
}

// 0x002f8a70 VMoveableBase::SetX
void VMoveableBase::SetX(int32_t rel_x) {
    SetRelaX(rel_x);
    if (m_move_id != 0) hmi_wrapper_igdt_set_x(m_move_id, (int16_t)(MoveAbsX() - MoveBaseX()));
}

// 0x002f8994 VMoveableBase::SetY
void VMoveableBase::SetY(int32_t rel_y) {
    SetRelaY(rel_y);
    if (m_move_id != 0) hmi_wrapper_igdt_set_y(m_move_id, (int16_t)(MoveAbsY() - MoveBaseY()));
}

// 0x002f85fc VMoveableBase::SetPos
void VMoveableBase::SetPos(int32_t rel_x, int32_t rel_y) {
    SetX(rel_x);
    SetY(rel_y);
}

} // namespace STDPlusLogicalControlLibrary
