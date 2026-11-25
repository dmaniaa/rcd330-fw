// Stock source: display/controls/controlbase/itembase.cpp
//
// STDPlusLogicalControlLibrary::ItemBase: the base of every control. Holds
// the rect the control was built with (its scene origin), its current size,
// its position relative to its parent (VMoveableBase) and its visibility
// (VVisibilityBase). Absolute x = relative x + parent's absolute x;
// displacement = absolute x - origin x, which is what a move-id group is
// translated by.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_ITEMBASE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_ITEMBASE_H

#include <stdint.h>

#include <vector>

#include <QtCore/QObject>

#include "display/controls/controlbase/vmoveablebase.h"
#include "display/controls/controlbase/vvisibilitybase.h"

namespace STDPlusLogicalControlLibrary {

class ItemBase;

// DEVIATION: stock has a separate abstract Item interface (the type the
// signals carry) that ItemBase implements, and ItemBase inherits
// VMoveableBase / VVisibilityBase. Every Item in opt/hmi is an ItemBase, so
// Item is an alias here, and the two helpers are members (their SetX would
// otherwise clash with ItemBase::SetX). Signal and slot signatures keep the
// stock text "Item*".
typedef ItemBase Item;
typedef std::vector<ItemBase*> ItemContainer;

class ItemBase : public QObject {
    Q_OBJECT
public:
    ItemBase(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t move_id, uint32_t visibility_id);
    virtual ~ItemBase();

    // Absolute position; SetX/SetY move the control (and its move-id group).
    virtual void SetX(int32_t abs_x);
    virtual void SetY(int32_t abs_y);
    int32_t GetAbsX() const;
    int32_t GetAbsY() const;
    // Position relative to the parent. Default: VMoveableBase::SetX/SetY
    // (relative x + move-id group). Stock overrides those VMoveableBase
    // slots directly (IgdtImageItem 0x002cedcc / 0x002ceeb8); ours routes
    // them through these. Names ours.
    virtual void SetRelX(int32_t rel_x);
    virtual void SetRelY(int32_t rel_y);
    // Stores the size and emits SigSizeChanged.
    virtual void SetSize(int32_t w, int32_t h);
    int32_t GetWidth() const;
    int32_t GetHeight() const;

    // The rect the control was built with.
    int32_t GetOrigX() const { return m_orig_x; }
    int32_t GetOrigY() const { return m_orig_y; }
    int32_t GetOrigWidth() const { return m_orig_w; }
    int32_t GetOrigHeight() const { return m_orig_h; }
    void SetOrigX(int32_t x);
    void SetOrigY(int32_t y);
    // Absolute position minus origin.
    int32_t GetDisplacementX() const;
    int32_t GetDisplacementY() const;

    // Re-parents (QObject parent too) keeping the absolute position.
    // Returns the previous parent.
    virtual Item* SetParent(Item* parent);
    ItemBase* GetParentItem() const { return m_parent; }

    virtual void SetVisibility(bool visible);
    bool IsVisible() const { return m_visibility.IsVisible(); }

    // Selects a state image. Stock calls this through its Image interface
    // on items it knows are images; non-image items ignore it here.
    virtual void SetDynIndex(int index) { (void)index; }

    // Connects this item's signals to receiver's On* slots (a ComplexControl).
    virtual void BindEventReceiver(QObject* receiver);

    VMoveableBase& Moveable() { return m_moveable; }
    const VMoveableBase& Moveable() const { return m_moveable; }
    VVisibilityBase& Visibility() { return m_visibility; }
    uint32_t GetMoveId() const { return m_moveable.GetMoveId(); }
    void SetMoveId(uint32_t id) { m_moveable.SetMoveId(id); }

signals:
    void SigSizeChanged(Item* item);
    void SigVisibilityChanged(Item* item);

private:
    class ItemMoveable : public VMoveableBase {
    public:
        ItemMoveable(ItemBase* owner, int32_t x, int32_t y, uint32_t move_id);
        // Forward to the owner's SetRelX / SetRelY.
        virtual void SetX(int32_t rel_x);
        virtual void SetY(int32_t rel_y);
        void BaseSetX(int32_t rel_x) { VMoveableBase::SetX(rel_x); }
        void BaseSetY(int32_t rel_y) { VMoveableBase::SetY(rel_y); }

    protected:
        virtual int32_t MoveAbsX() const;
        virtual int32_t MoveBaseX() const;
        virtual int32_t MoveAbsY() const;
        virtual int32_t MoveBaseY() const;

    private:
        ItemBase* m_owner;
    };

    ItemMoveable m_moveable;        // stock base at +0x08
    VVisibilityBase m_visibility;   // stock base at +0x10
    int32_t m_w, m_h;               // +0x20, +0x24
    int32_t m_orig_x, m_orig_y;     // +0x28, +0x2c
    int32_t m_orig_w, m_orig_h;     // +0x30, +0x34
    QObject* m_receiver;            // +0x38
    ItemBase* m_parent;             // ours: stock dynamic_casts QObject::parent()
};

} // namespace STDPlusLogicalControlLibrary

#endif
