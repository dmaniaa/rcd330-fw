// Stock source: display/controls/controlbase/complexcontrol.cpp
#include "display/controls/controlbase/complexcontrol.h"

namespace STDPlusLogicalControlLibrary {

// 0x002c7efc ComplexControl::ComplexControl
ComplexControl::ComplexControl(int32_t x, int32_t y, int32_t w, int32_t h, const ItemContainer& children,
                               uint32_t move_id, uint32_t visibility_id)
    : ItemBase(x, y, w, h, move_id, visibility_id), m_children(children) {
    for (ItemContainer::iterator it = m_children.begin(); it != m_children.end(); ++it) {
        if (*it) (*it)->SetParent(this);
    }
}

// No origin tag: stock's destructor is not exported as a separate function.
ComplexControl::~ComplexControl() {}

// TODO(phase3): stock then calls Item vtable slot +0x48 with (w, h) on every
// child (not identified yet; a plain child SetSize would stretch every page
// child to the page size), so ours only resizes the control itself.
// 0x002c7714 ComplexControl::SetSize
void ComplexControl::SetSize(int32_t w, int32_t h) {
    ItemBase::SetSize(w, h);
}

// 0x002c80d0 ComplexControl::AddChild
void ComplexControl::AddChild(ItemBase* child) {
    if (!child) return;
    m_children.push_back(child);
    child->BindEventReceiver(this);
    child->SetParent(this);
}

// 0x002c7d84 ComplexControl::GetChildCount
int ComplexControl::GetChildCount() const {
    return (int)m_children.size();
}

// No origin tag: the default On* slots are empty stubs in stock.
void ComplexControl::OnSizeChanged(Item*) {}
// No origin tag: empty default slot.
void ComplexControl::OnVisibilityChanged(Item*) {}
// No origin tag: empty default slot.
void ComplexControl::OnCreate(Item*) {}
// No origin tag: empty default slot.
void ComplexControl::OnFocus(Item*, bool) {}
// No origin tag: empty default slot.
void ComplexControl::OnPress(Item*, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnRelease(Item*, int32_t, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnClicked(Item*, int32_t, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnMove(Item*, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnHold(Item*, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnLongPress(Item*, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnExLongPress(Item*, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnUltraLongPress(Item*, int32_t, int32_t) {}
// No origin tag: empty default slot.
void ComplexControl::OnLeave(Item*, int32_t, int32_t) {}

} // namespace STDPlusLogicalControlLibrary
