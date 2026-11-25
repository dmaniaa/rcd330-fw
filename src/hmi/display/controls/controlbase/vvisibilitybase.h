// Stock source: display/controls/controlbase/vvisibilitybase.cpp
//
// Shows or hides a control: through its visibility branch (index 1 shown,
// 0 hidden) when it has one, else by moving its move-id group off screen
// (x = -10000) and back.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_VVISIBILITYBASE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_VVISIBILITYBASE_H

#include <stdint.h>

namespace STDPlusLogicalControlLibrary {

class VMoveableBase;

// DEVIATION: stock's m_pImpl_ block is held inline (see VMoveableBase).
class VVisibilityBase {
public:
    VVisibilityBase(uint32_t visibility_id, VMoveableBase* moveable);
    virtual ~VVisibilityBase();

    virtual void SetVisibility(bool visible);
    bool IsVisible() const;
    uint32_t GetVisibilityId() const;
    void SetVisibilityId(uint32_t id);
    VMoveableBase* GetMoveablePtr() const;
    void SetMoveablePtr(VMoveableBase* moveable);

private:
    bool m_visible;
    uint32_t m_visibility_id;
    VMoveableBase* m_moveable;
};

} // namespace STDPlusLogicalControlLibrary

#endif
