// Stock source: display/controls/controlbase/complexcontrol.cpp
//
// A control made of child items (pages derive from it). AddChild re-parents
// the child and binds the child's signals to this control's On* slots
// (ItemBase::BindEventReceiver / ButtonBase::BindEventReceiver).
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_COMPLEXCONTROL_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_COMPLEXCONTROL_H

#include <stdint.h>

#include "display/controls/controlbase/itembase.h"

namespace STDPlusLogicalControlLibrary {

class ComplexControl : public ItemBase {
    Q_OBJECT
public:
    ComplexControl(int32_t x, int32_t y, int32_t w, int32_t h, const ItemContainer& children, uint32_t move_id,
                   uint32_t visibility_id);
    virtual ~ComplexControl();

    virtual void SetSize(int32_t w, int32_t h);
    // DEVIATION: stock takes an Item* and dynamic_casts it to ItemBase.
    void AddChild(ItemBase* child);
    int GetChildCount() const;
    const ItemContainer& GetChildren() const { return m_children; }

public slots:
    // Default handlers do nothing; pages override the ones they use.
    virtual void OnSizeChanged(Item* item);
    virtual void OnVisibilityChanged(Item* item);
    virtual void OnCreate(Item* item);
    virtual void OnFocus(Item* item, bool focus);
    virtual void OnPress(Item* item, int32_t x, int32_t y);
    virtual void OnRelease(Item* item, int32_t x, int32_t y, int32_t press_type);
    virtual void OnClicked(Item* item, int32_t x, int32_t y, int32_t press_type);
    virtual void OnMove(Item* item, int32_t x, int32_t y);
    virtual void OnHold(Item* item, int32_t x, int32_t y);
    virtual void OnLongPress(Item* item, int32_t x, int32_t y);
    virtual void OnExLongPress(Item* item, int32_t x, int32_t y);
    virtual void OnUltraLongPress(Item* item, int32_t x, int32_t y);
    virtual void OnLeave(Item* item, int32_t x, int32_t y);

private:
    ItemContainer m_children;   // +0x50
};

} // namespace STDPlusLogicalControlLibrary

#endif
