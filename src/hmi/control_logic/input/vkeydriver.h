// Stock source: control_logic/input/vkeydriver.cpp
//
// Faceplate buttons: 16-byte EV_KEY records from /dev/event0 (codes 1..10,
// see input_decoders.h). Each read runs readKeyDriver on this task's thread
// and re-queues itself through switchToRead, so the blocking read() never
// holds up another thread. Accepted keys go to
// VControlTask::PostKeyEvent(code, value).
//
// Start-up (main 0x00010e58): new VKeyDriver; Init(); Start().
#ifndef RCD330_HMI_CONTROL_LOGIC_INPUT_VKEYDRIVER_H
#define RCD330_HMI_CONTROL_LOGIC_INPUT_VKEYDRIVER_H

#include <stdint.h>

#include "control_logic/input/input_decoders.h"
#include "service/utility/vbasetask.h"

class VKeyDriver : public VBaseTask {
    Q_OBJECT
public:
    VKeyDriver();

    // Stock returns the object the constructor registered (NULL before).
    static VKeyDriver* Instance();

    // Connects the slots and emits SigInitialize (queued until Start()).
    void Init();

    // Any thread (stock reads the byte unlocked).
    bool IsTuneOkDown() const;
    // Active (lock-holding) key code, +0x10. Read by the IPCL hard-key filter
    // and written by key re-posts that bypass this driver.
    uint16_t GetKeyCode() const;
    void SetKeyCode(uint16_t code);

signals:
    void switchToRead();

protected slots:
    virtual void InitializeSlot();

private slots:
    void readKeyDriver();

private:
    void RegisterSignals();

    KeyFilterState m_state;  // active_code +0x10, tune_ok_down +0x18
    int m_fd;                // +0x14
    bool m_logged_short;     // ours: log-once flags
};

#endif  // RCD330_HMI_CONTROL_LOGIC_INPUT_VKEYDRIVER_H
