// Stock source: display/controls/controlbase/igdtbuttonbase.cpp
//
// A ButtonBase bound to an IGDT touch group (touch id). The page routes
// each touch event for that id to IgdtTouchScreenHandler, which calls
// TouchEventHandler: a cancel flag (bit 31) first leaves, then the low byte
// dispatches 0 move, 1 press, 3 release (+ click unless cancelled). After a
// click, DisableDbClick(ms) blocks further presses/releases for ms (they
// are turned into moves).
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_IGDTBUTTONBASE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_IGDTBUTTONBASE_H

#include <stdint.h>

#include <QtCore/QTimer>

#include "display/controls/controlbase/buttonbase.h"
#include "display/igdt/igdt_touch.h"

namespace STDPlusLogicalControlLibrary {

class IgdtButtonBase : public ButtonBase {
    Q_OBJECT
public:
    IgdtButtonBase(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t move_id, uint32_t visibility_id,
                   uint32_t touch_id);
    virtual ~IgdtButtonBase();

    uint32_t GetTouchId() const;
    // Touch rect in scene coordinates: (x, y) minus this button's
    // displacement (the touch group moves with the move-id group).
    void SetTouchArea(int32_t x, int32_t y, int32_t w, int32_t h);
    // Blocks presses for ms after a click; 0 (default) disables the block.
    void DisableDbClick(int ms);
    virtual void StopPressTimers();

    virtual void TouchEventHandler(IgdtTouchEvent* ev);
    // True (and the event handled) when touch_id is this button's.
    bool IgdtTouchScreenHandler(uint32_t touch_id, IgdtTouchEvent* ev);

private:
    // Stock m_pImpl_ (+0x94): touch id, double-click QTimer, block interval.
    uint32_t m_touch_id;
    QTimer m_dbl_click_timer;
    int m_dbl_click_ms;
};

} // namespace STDPlusLogicalControlLibrary

#endif
