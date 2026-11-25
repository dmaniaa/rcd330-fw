// Stock source: display/controls/controlbase/buttonbase.cpp
#include "display/controls/controlbase/buttonbase.h"

#include "service/osal/osal_api.h"

namespace STDPlusLogicalControlLibrary {

namespace {

const int kHoldFirstMs = 1000;
const int kHoldRepeatMs = 250;
const int kLongPressMs = 2000;
const int kExLongPressMs = 3000;
const int kUltraLongPressMs = 5000;

} // namespace

// 0x002c6e28 ButtonBase::ButtonBase
ButtonBase::ButtonBase(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t move_id, uint32_t visibility_id)
    : ItemBase(x, y, w, h, move_id, visibility_id),
      m_state(STATE_NORMAL),
      m_selected(false),
      m_press_x(0),
      m_press_y(0),
      m_hold_timer(0),
      m_long_timer(0),
      m_press_type(PRESS_SHORT),
      m_text_color(0xffffffffu),
      m_disabled_color(0x73737373u) {
    connect(&m_hold_timer, SIGNAL(timeout()), this, SLOT(OnHolding()));
    connect(&m_long_timer, SIGNAL(timeout()), this, SLOT(OnLongPressTimeOut()));
}

// No origin tag: stock's destructor is not exported as a separate function.
ButtonBase::~ButtonBase() {}

// 0x002c6fdc ButtonBase::SetState
void ButtonBase::SetState(int state) {
    m_state = state;
}

// 0x002c6ffc ButtonBase::SetSelected
void ButtonBase::SetSelected(bool selected) {
    m_selected = selected;
}

// 0x002c7040 ButtonBase::SetTextColors
void ButtonBase::SetTextColors(uint32_t normal, uint32_t disabled) {
    m_text_color = normal;
    m_disabled_color = disabled;
}

// 0x002c7054 ButtonBase::GetTextColor
uint32_t ButtonBase::GetTextColor() const {
    return m_text_color;
}

// 0x002c7074 ButtonBase::BindEventReceiver
void ButtonBase::BindEventReceiver(QObject* receiver) {
    ItemBase::BindEventReceiver(receiver);
    if (!receiver) return;
    connect(this, SIGNAL(SigCreate(Item*)), receiver, SLOT(OnCreate(Item*)));
    connect(this, SIGNAL(SigFocus(Item*, bool)), receiver, SLOT(OnFocus(Item*, bool)));
    connect(this, SIGNAL(SigPress(Item*, int32_t, int32_t)), receiver, SLOT(OnPress(Item*, int32_t, int32_t)));
    connect(this, SIGNAL(SigRelease(Item*, int32_t, int32_t, int32_t)), receiver,
            SLOT(OnRelease(Item*, int32_t, int32_t, int32_t)));
    connect(this, SIGNAL(SigClicked(Item*, int32_t, int32_t, int32_t)), receiver,
            SLOT(OnClicked(Item*, int32_t, int32_t, int32_t)));
    connect(this, SIGNAL(SigMove(Item*, int32_t, int32_t)), receiver, SLOT(OnMove(Item*, int32_t, int32_t)));
    connect(this, SIGNAL(SigHold(Item*, int32_t, int32_t)), receiver, SLOT(OnHold(Item*, int32_t, int32_t)));
    connect(this, SIGNAL(SigLongPress(Item*, int32_t, int32_t)), receiver,
            SLOT(OnLongPress(Item*, int32_t, int32_t)));
    connect(this, SIGNAL(SigExLongPress(Item*, int32_t, int32_t)), receiver,
            SLOT(OnExLongPress(Item*, int32_t, int32_t)));
    connect(this, SIGNAL(SigUltraLongPress(Item*, int32_t, int32_t)), receiver,
            SLOT(OnUltraLongPress(Item*, int32_t, int32_t)));
    connect(this, SIGNAL(SigLeave(Item*, int32_t, int32_t)), receiver, SLOT(OnLeave(Item*, int32_t, int32_t)));
}

// No origin tag: OnPress (stock VA 0x002c7288) is not a separate function
// in the decompilation export.
void ButtonBase::OnPress(int32_t x, int32_t y) {
    if (IsDisabled()) return;
    m_hold_timer.setInterval(kHoldFirstMs);
    m_hold_timer.start();
    m_long_timer.setInterval(kLongPressMs);
    m_long_timer.start();
    m_press_x = x;
    m_press_y = y;
    SetState(STATE_PRESSED);
    m_press_type = PRESS_SHORT;
    emit SigPress(this, x, y);
}

// 0x002c7320 ButtonBase::OnRelease
void ButtonBase::OnRelease(int32_t x, int32_t y) {
    m_hold_timer.stop();
    m_long_timer.stop();
    if (IsDisabled()) return;
    SetState(STATE_NORMAL);
    emit SigRelease(this, x, y, m_press_type);
}

// 0x002c7398 ButtonBase::OnClicked
void ButtonBase::OnClicked(int32_t x, int32_t y) {
    if (IsDisabled()) return;
    emit SigClicked(this, x, y, m_press_type);
}

// 0x002c73f0 ButtonBase::OnLeave
void ButtonBase::OnLeave(int32_t x, int32_t y) {
    m_hold_timer.stop();
    m_long_timer.stop();
    if (!IsDisabled()) SetState(STATE_NORMAL);
    emit SigLeave(this, x, y);
}

// 0x002c7458 ButtonBase::OnMove
void ButtonBase::OnMove(int32_t x, int32_t y) {
    m_press_x = x;
    m_press_y = y;
    emit SigMove(this, x, y);
}

// DEVIATION: stock logs through log_optional; ours uses log_simple.
// 0x002c748c ButtonBase::OnHold
void ButtonBase::OnHold(int32_t x, int32_t y) {
    log_simple("INFO", " ==== HMI ==== INFO: ButtonBase::OnHold %d, %d", x, y);
    m_press_type = PRESS_HOLD;
    emit SigHold(this, x, y);
}

// 0x002c7534 ButtonBase::OnLongPress
void ButtonBase::OnLongPress(int32_t x, int32_t y) {
    emit SigLongPress(this, x, y);
}

// 0x002c755c ButtonBase::OnExLongPress
void ButtonBase::OnExLongPress(int32_t x, int32_t y) {
    emit SigExLongPress(this, x, y);
}

// 0x002c7584 ButtonBase::OnUltraLongPress
void ButtonBase::OnUltraLongPress(int32_t x, int32_t y) {
    emit SigUltraLongPress(this, x, y);
}

// Name is ours.
// 0x002c76a0 ButtonBase::StopPressTimers
void ButtonBase::StopPressTimers() {
    if (m_hold_timer.isActive()) m_hold_timer.stop();
    if (m_long_timer.isActive()) m_long_timer.stop();
}

// 0x002c75ac ButtonBase::OnHolding
void ButtonBase::OnHolding() {
    m_hold_timer.stop();
    m_hold_timer.setInterval(kHoldRepeatMs);
    m_hold_timer.start();
    OnHold(m_press_x, m_press_y);
}

// The long-press timer runs 2000 -> 3000 -> 5000 ms; each step fires the
// next handler and sets the press type to 2 / 3 / 4.
// 0x002c75f0 ButtonBase::OnLongPressTimeOut
void ButtonBase::OnLongPressTimeOut() {
    m_long_timer.stop();
    const int interval = m_long_timer.interval();
    if (interval == kLongPressMs) {
        m_long_timer.setInterval(kExLongPressMs);
        m_long_timer.start();
        OnLongPress(m_press_x, m_press_y);
        m_press_type = PRESS_LONG;
    } else if (interval == kExLongPressMs) {
        m_long_timer.setInterval(kUltraLongPressMs);
        m_long_timer.start();
        OnExLongPress(m_press_x, m_press_y);
        m_press_type = PRESS_EX_LONG;
    } else if (interval == kUltraLongPressMs) {
        OnUltraLongPress(m_press_x, m_press_y);
        m_press_type = PRESS_ULTRA_LONG;
    }
}

} // namespace STDPlusLogicalControlLibrary
