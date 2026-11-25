// Stock source: display/multitouch/vmultitouch.cpp
//
// Touchscreen through the device tslib (libts-1.0.so.0, 24-byte samples).
// Init() opens and configures the device on the caller's thread (up to 15
// tries, 200 ms apart); Run() starts the task thread, whose Initialize()
// slot never returns: it blocks in ts_read(..., 3) and emits
// SigTouchEvent(VMULTITOUCH_DATA) (queued to receivers on other threads).
//
// Start-up (VDisplayTask 0x00065238 / 0x00063584): new VMultiTouch; Init();
// SetMoveInterval(20); ... Run().
#ifndef RCD330_HMI_DISPLAY_MULTITOUCH_VMULTITOUCH_H
#define RCD330_HMI_DISPLAY_MULTITOUCH_VMULTITOUCH_H

#include <stdint.h>

#include <QtCore/QMetaType>
#include <QtCore/QTime>

#include "control_logic/input/input_decoders.h"
#include "service/tslib/tslib_api.h"
#include "service/utility/vbasetask.h"

class VMultiTouch : public VBaseTask {
    Q_OBJECT
public:
    VMultiTouch();

    // TSLIB_TSDEVICE, else /dev/input/event0 then /dev/touchscreen/ucb1x00;
    // ts_config on success. Blocks up to 3 s when the device is missing.
    void Init();
    // Starts the thread and emits SigInitialize.
    void Run();
    // Move report throttle in ms (0 = every move). Stock default 20.
    void SetMoveInterval(int ms);

    bool IsOpen() const { return m_ts != 0; }

signals:
    void SigTouchEvent(VMULTITOUCH_DATA data);

private slots:
    void Initialize();

private:
    bool m_raw_mode;            // +0x10 (initially 1; raw path is phase 3)
    MultiTouchTracker m_tracker;  // +0x11, +0x14, +0x18, +0x24..
    QTime m_clock;              // ours: feeds m_tracker.now_ms
    struct tsdev* m_ts;         // +0x20
};

#endif  // RCD330_HMI_DISPLAY_MULTITOUCH_VMULTITOUCH_H
