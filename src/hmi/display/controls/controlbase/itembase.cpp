// Stock source: display/controls/controlbase/itembase.cpp
#include "display/controls/controlbase/itembase.h"

#include "service/osal/osal_api.h"

#define ITEMBASE_NULL_LOG(what, func, line)                                                       \
    do {                                                                                          \
        log_simple("ERROR", " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, \
                   func, line);                                                                   \
        printlog(3, "ERROR", "display/controls/controlbase/itembase.cpp", func, line,             \
                 " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func,      \
                 line);                                                                           \
    } while (0)

namespace STDPlusLogicalControlLibrary {

// No origin tag: ItemMoveable is ours (stock ItemBase inherits VMoveableBase).
ItemBase::ItemMoveable::ItemMoveable(ItemBase* owner, int32_t x, int32_t y, uint32_t move_id)
    : VMoveableBase(x, y, move_id), m_owner(owner) {}

// No origin tag: ours (forwards the VMoveableBase slot to the owner).
void ItemBase::ItemMoveable::SetX(int32_t rel_x) {
    m_owner->SetRelX(rel_x);
}

// No origin tag: ours (forwards the VMoveableBase slot to the owner).
void ItemBase::ItemMoveable::SetY(int32_t rel_y) {
    m_owner->SetRelY(rel_y);
}

// No origin tag: the VMoveableBase position hooks of ItemBase are inline
// vtable thunks in stock.
int32_t ItemBase::ItemMoveable::MoveAbsX() const {
    return m_owner->GetAbsX();
}

// No origin tag: see MoveAbsX.
int32_t ItemBase::ItemMoveable::MoveBaseX() const {
    const ItemBase* p = m_owner->GetParentItem();
    return m_owner->GetOrigX() + (p ? p->GetDisplacementX() : 0);
}

// No origin tag: see MoveAbsX.
int32_t ItemBase::ItemMoveable::MoveAbsY() const {
    return m_owner->GetAbsY();
}

// No origin tag: see MoveAbsX.
int32_t ItemBase::ItemMoveable::MoveBaseY() const {
    const ItemBase* p = m_owner->GetParentItem();
    return m_owner->GetOrigY() + (p ? p->GetDisplacementY() : 0);
}

// 0x002d0b50 ItemBase::ItemBase
ItemBase::ItemBase(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t move_id, uint32_t visibility_id)
    : QObject(0),
      m_moveable(this, x, y, move_id),
      m_visibility(visibility_id, &m_moveable),
      m_w(w),
      m_h(h),
      m_orig_x(x),
      m_orig_y(y),
      m_orig_w(w),
      m_orig_h(h),
      m_receiver(0),
      m_parent(0) {}

// No origin tag: stock's destructor is not exported as a separate function.
ItemBase::~ItemBase() {}

// 0x002d0c48 ItemBase::SetX
void ItemBase::SetX(int32_t abs_x) {
    m_moveable.SetX(abs_x - (m_parent ? m_parent->GetAbsX() : 0));
}

// 0x002d0cb8 ItemBase::SetY
void ItemBase::SetY(int32_t abs_y) {
    m_moveable.SetY(abs_y - (m_parent ? m_parent->GetAbsY() : 0));
}

// No origin tag: VMoveableBase::SetX (stock's default for this slot).
void ItemBase::SetRelX(int32_t rel_x) {
    m_moveable.BaseSetX(rel_x);
}

// No origin tag: VMoveableBase::SetY (stock's default for this slot).
void ItemBase::SetRelY(int32_t rel_y) {
    m_moveable.BaseSetY(rel_y);
}

// 0x002d0d28 ItemBase::GetAbsX
int32_t ItemBase::GetAbsX() const {
    return m_moveable.GetRelaX() + (m_parent ? m_parent->GetAbsX() : 0);
}

// 0x002d0d94 ItemBase::GetAbsY
int32_t ItemBase::GetAbsY() const {
    return m_moveable.GetRelaY() + (m_parent ? m_parent->GetAbsY() : 0);
}

// 0x002d0e00 ItemBase::SetSize
void ItemBase::SetSize(int32_t w, int32_t h) {
    m_w = w;
    m_h = h;
    emit SigSizeChanged(this);
}

// 0x002d0e2c ItemBase::GetWidth
int32_t ItemBase::GetWidth() const {
    return m_w;
}

// 0x002d0e3c ItemBase::GetHeight
int32_t ItemBase::GetHeight() const {
    return m_h;
}

// 0x002d11a4 ItemBase::SetOrigX
void ItemBase::SetOrigX(int32_t x) {
    m_orig_x = x;
}

// 0x002d11b4 ItemBase::SetOrigY
void ItemBase::SetOrigY(int32_t y) {
    m_orig_y = y;
}

// No origin tag: inlined in stock (GetAbsX() - origin x).
int32_t ItemBase::GetDisplacementX() const {
    return GetAbsX() - m_orig_x;
}

// No origin tag: inlined in stock (GetAbsY() - origin y).
int32_t ItemBase::GetDisplacementY() const {
    return GetAbsY() - m_orig_y;
}

// DEVIATION: stock asserts on a NULL parent ("dynamic_cast<QObject*>(parent)
// != __null", itembase.cpp:218); ours logs and keeps the current parent.
// 0x002d0e78 ItemBase::SetParent
Item* ItemBase::SetParent(Item* parent) {
    if (!parent) {
        ITEMBASE_NULL_LOG("parent", "SetParent", 0xda);
        return m_parent;
    }
    ItemBase* old = m_parent;
    const int32_t abs_x = GetAbsX();
    const int32_t abs_y = GetAbsY();
    m_moveable.SetRelaX(abs_x - parent->GetAbsX());
    m_moveable.SetRelaY(abs_y - parent->GetAbsY());
    m_parent = parent;
    QObject::setParent(parent);
    return old;
}

// 0x002d1290 ItemBase::SetVisibility
void ItemBase::SetVisibility(bool visible) {
    const bool was = m_visibility.IsVisible();
    m_visibility.SetVisibility(visible);
    if (was != visible) emit SigVisibilityChanged(this);
}

// 0x002d12e8 ItemBase::BindEventReceiver
void ItemBase::BindEventReceiver(QObject* receiver) {
    m_receiver = receiver;
    if (!receiver) {
        ITEMBASE_NULL_LOG("pqobject", "BindEventReceiver", 0x19e);
        return;
    }
    QObject::connect(this, SIGNAL(SigSizeChanged(Item*)), receiver, SLOT(OnSizeChanged(Item*)));
    QObject::connect(this, SIGNAL(SigVisibilityChanged(Item*)), receiver, SLOT(OnVisibilityChanged(Item*)));
}

} // namespace STDPlusLogicalControlLibrary
