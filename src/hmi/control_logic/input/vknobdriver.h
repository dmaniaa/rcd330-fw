// Stock source: control_logic/input/vknobdriver.cpp
//
// Tune knob: 4-byte words from /dev/tuner_knob (0x32 up, 0x33 down). Each
// detent posts (7 | 8, 1) to VControlTask::PostKeyEvent unless TUNE_OK is
// held (VKeyDriver::IsTuneOkDown); fast bursts add extra detents
// (CalculateFastRotateNum). readKnobDriver blocks in select() on this
// task's thread and re-queues itself through switchToRead.
//
// Start-up (main 0x00010e58), after VKeyDriver: new VKnobDriver; Init(); Run().
#ifndef RCD330_HMI_CONTROL_LOGIC_INPUT_VKNOBDRIVER_H
#define RCD330_HMI_CONTROL_LOGIC_INPUT_VKNOBDRIVER_H

#include <stdint.h>

#include <QtCore/QTime>

#include "control_logic/input/input_decoders.h"
#include "service/utility/vbasetask.h"

class VKnobDriver : public VBaseTask {
    Q_OBJECT
public:
    VKnobDriver();

    // Connects the slots (stock also registers the UINT8 typedef and
    // connects SigKeyEventNotify to VDisplayTask; see the .cpp).
    void Init();
    // Starts the thread and emits SigInitialize.
    void Run();

signals:
    void switchToRead();

protected slots:
    virtual void InitializeSlot();

private slots:
    void readKnobDriver();

private:
    void PostDetent(uint8_t key);
    void CalculateFastRotateNum(uint8_t direction);
    void DuplicateRotation(uint8_t direction, uint32_t count);

    int32_t m_word;              // +0x10
    int m_fd;                    // +0x14
    KnobRotateState m_rotate;    // count +0x18, direction +0x1c
    QTime m_rotate_time;         // +0x20
    bool m_logged_short;         // ours: log-once flags
    bool m_logged_word;
};

#endif  // RCD330_HMI_CONTROL_LOGIC_INPUT_VKNOBDRIVER_H
