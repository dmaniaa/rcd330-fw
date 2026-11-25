// Stock source: display/controls/controlbase/igdtbuttonbase.cpp
#include "display/controls/controlbase/igdtbuttonbase.h"

#include "display/igdt/extend/igdt_wrapper.h"
#include "display/igdt/igdt_tables.h"

namespace STDPlusLogicalControlLibrary {

// 0x002e9d68 IgdtButtonBase::IgdtButtonBase
IgdtButtonBase::IgdtButtonBase(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t move_id,
                               uint32_t visibility_id, uint32_t touch_id)
    : ButtonBase(x, y, w, h, move_id, visibility_id), m_touch_id(touch_id), m_dbl_click_timer(0), m_dbl_click_ms(0) {
    m_dbl_click_timer.setSingleShot(true);
}

// 0x002e9e28 IgdtButtonBase::~IgdtButtonBase
IgdtButtonBase::~IgdtButtonBase() {}

// 0x002e9fd4 IgdtButtonBase::GetTouchId
uint32_t IgdtButtonBase::GetTouchId() const {
    return m_touch_id;
}

// 0x002ea060 IgdtButtonBase::SetTouchArea
void IgdtButtonBase::SetTouchArea(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (m_touch_id == 0) return;
    IgdtClipRect r;
    hmi_wrapper_igdt_get_touch_area(m_touch_id, &r);
    r.x = (int16_t)(x - GetDisplacementX());
    r.y = (int16_t)(y - GetDisplacementY());
    r.w = (int16_t)w;
    r.h = (int16_t)h;
    hmi_wrapper_igdt_set_touch_area(m_touch_id, r);
}

// 0x002ea1f0 IgdtButtonBase::DisableDbClick
void IgdtButtonBase::DisableDbClick(int ms) {
    m_dbl_click_ms = ms;
}

// 0x002ea150 IgdtButtonBase::StopPressTimers
void IgdtButtonBase::StopPressTimers() {
    ButtonBase::StopPressTimers();
    if (m_dbl_click_timer.isActive()) m_dbl_click_timer.stop();
}

// STOCK-QUIRK: the drag-out test compares the whole word with 0x80000003,
// so a cancelled release with any other flag bit still clicks unless it
// carries 0x40000000. control_layout's EvalButtonTouch mirrors this.
// 0x002ea27c IgdtButtonBase::TouchEventHandler
void IgdtButtonBase::TouchEventHandler(IgdtTouchEvent* ev) {
    if (!ev) return;
    if ((int32_t)ev->type < 0) OnLeave(ev->x, ev->y);
    if (m_dbl_click_timer.isActive()) ev->type &= 0xffffff00u;
    switch (ev->type & 0xff) {
    case IGDT_TOUCH_MOVE:
        OnMove(ev->x, ev->y);
        break;
    case IGDT_TOUCH_PRESS:
        OnPress(ev->x, ev->y);
        break;
    case IGDT_TOUCH_RELEASE:
        OnRelease(ev->x, ev->y);
        if (ev->type != (IGDT_TOUCH_CANCEL_OUTSIDE | IGDT_TOUCH_RELEASE) &&
            (ev->type & IGDT_TOUCH_CANCEL_PAGE) == 0) {
            OnClicked(ev->x, ev->y);
            if (m_dbl_click_ms != 0) {
                m_dbl_click_timer.setInterval(m_dbl_click_ms);
                m_dbl_click_timer.start();
            }
        }
        break;
    default:
        break;
    }
}

// 0x002ea4a0 IgdtButtonBase::IgdtTouchScreenHandler
bool IgdtButtonBase::IgdtTouchScreenHandler(uint32_t touch_id, IgdtTouchEvent* ev) {
    if (m_touch_id != touch_id) return false;
    TouchEventHandler(ev);
    return true;
}

} // namespace STDPlusLogicalControlLibrary
