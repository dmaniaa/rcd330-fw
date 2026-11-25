// Stock source: display/controls/controlbase/buttonbase.cpp
//
// Press/release/click handling shared by every button. States: 0 normal,
// 1 pressed, 2 disabled (a disabled button ignores press/release/click).
// Selection is a separate flag. A press starts the hold timer (1000 ms,
// then OnHold every 250 ms) and the long-press timer (2000 ms long,
// +3000 ms extra long, +5000 ms ultra long).
//
// Threading: everything here runs on the thread that delivers the touch
// event (the main thread, see IgdtButtonBase); signals use direct
// connections, so receivers run synchronously inside TouchEventHandler.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_BUTTONBASE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_BUTTONBASE_H

#include <stdint.h>

#include <QtCore/QTimer>

#include "display/controls/controlbase/itembase.h"

namespace STDPlusLogicalControlLibrary {

class ButtonBase : public ItemBase {
    Q_OBJECT
public:
    enum { STATE_NORMAL = 0, STATE_PRESSED = 1, STATE_DISABLED = 2 };
    // Hold flag passed as press_type in SigRelease / SigClicked.
    enum { PRESS_SHORT = 0, PRESS_HOLD = 1, PRESS_LONG = 2, PRESS_EX_LONG = 3, PRESS_ULTRA_LONG = 4 };

    ButtonBase(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t move_id, uint32_t visibility_id);
    virtual ~ButtonBase();

    virtual void SetState(int state);
    int GetState() const { return m_state; }
    bool IsDisabled() const { return m_state == STATE_DISABLED; }
    virtual void SetSelected(bool selected);
    bool IsSelected() const { return m_selected; }
    void SetTextColors(uint32_t normal, uint32_t disabled);
    uint32_t GetTextColor() const;
    uint32_t GetDisabledTextColor() const { return m_disabled_color; }

    // Item signals plus the 11 button signals to receiver's slots.
    virtual void BindEventReceiver(QObject* receiver);

    // Touch handlers (IgdtButtonBase::TouchEventHandler calls them).
    virtual void OnPress(int32_t x, int32_t y);
    virtual void OnRelease(int32_t x, int32_t y);
    virtual void OnClicked(int32_t x, int32_t y);
    virtual void OnLeave(int32_t x, int32_t y);
    virtual void OnMove(int32_t x, int32_t y);
    virtual void OnHold(int32_t x, int32_t y);
    virtual void OnLongPress(int32_t x, int32_t y);
    virtual void OnExLongPress(int32_t x, int32_t y);
    virtual void OnUltraLongPress(int32_t x, int32_t y);
    // Stops the hold and long-press timers.
    virtual void StopPressTimers();

signals:
    void SigCreate(Item* item);
    void SigFocus(Item* item, bool focus);
    void SigPress(Item* item, int32_t x, int32_t y);
    void SigRelease(Item* item, int32_t x, int32_t y, int32_t press_type);
    void SigClicked(Item* item, int32_t x, int32_t y, int32_t press_type);
    void SigMove(Item* item, int32_t x, int32_t y);
    void SigHold(Item* item, int32_t x, int32_t y);
    void SigLongPress(Item* item, int32_t x, int32_t y);
    void SigExLongPress(Item* item, int32_t x, int32_t y);
    void SigUltraLongPress(Item* item, int32_t x, int32_t y);
    void SigLeave(Item* item, int32_t x, int32_t y);

private slots:
    void OnHolding();
    void OnLongPressTimeOut();

private:
    int m_state;               // +0x44
    bool m_selected;           // +0x48
    int32_t m_press_x;         // +0x4c
    int32_t m_press_y;         // +0x50
    QTimer m_hold_timer;       // +0x54
    QTimer m_long_timer;       // +0x6c
    int32_t m_press_type;      // +0x84
    uint32_t m_text_color;     // +0x88
    uint32_t m_disabled_color; // +0x8c
};

} // namespace STDPlusLogicalControlLibrary

#endif
