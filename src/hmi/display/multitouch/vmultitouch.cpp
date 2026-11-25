// Stock source: display/multitouch/vmultitouch.cpp
#include "display/multitouch/vmultitouch.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "service/osal/osal_api.h"

#define TOUCH_LOG(func, line, ...)                                                          \
    do {                                                                                    \
        log_simple("INFO", __VA_ARGS__);                                                    \
        printlog(3, "INFO", "display/multitouch/vmultitouch.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

const int kOpenTries = 15;
const useconds_t kOpenRetryUs = 200000;
const int kDefaultMoveIntervalMs = 0x14;
// DEVIATION: stock retries a failing ts_read immediately (busy loop).
const useconds_t kReadErrorBackoffUs = 200000;

struct tsdev* OpenWithRetry(const char* primary, const char* fallback, int line) {
    for (int attempt = 1; attempt <= kOpenTries; ++attempt) {
        struct tsdev* ts = ts_open(primary, 0);
        if (ts != 0) {
            return ts;
        }
        if (fallback != 0) {
            ts = ts_open(fallback, 0);
            if (ts != 0) {
                return ts;
            }
        }
        TOUCH_LOG("Init", line, " ==== HMI ==== INFO: ts_open error retry %d times \n", attempt);
        usleep(kOpenRetryUs);
    }
    return 0;
}

}  // namespace

// 0x000237f4 VMultiTouch::VMultiTouch
// Stock connects SigInitialize here; the VMULTITOUCH_DATA registration
// (stock does it beside VDisplayTask's other types) is done here too.
VMultiTouch::VMultiTouch() : m_raw_mode(true), m_ts(0) {
    memset(&m_tracker, 0, sizeof(m_tracker));
    m_tracker.move_interval_ms = kDefaultMoveIntervalMs;
    qRegisterMetaType<VMULTITOUCH_DATA>("VMULTITOUCH_DATA");
    connect(this, SIGNAL(SigInitialize()), this, SLOT(Initialize()));
}

// 0x000238d4 VMultiTouch::Init
// Stock also allocates the move QTime (+0x1c); ours keeps m_clock instead.
void VMultiTouch::Init() {
    const char* dev = getenv("TSLIB_TSDEVICE");
    if (dev != 0) {
        m_ts = OpenWithRetry(dev, 0, 0x5b);
    } else {
        m_ts = OpenWithRetry("/dev/input/event0", "/dev/touchscreen/ucb1x00", 0x71);
    }
    if (m_ts == 0) {
        TOUCH_LOG("Init", 0x79, " ==== HMI ==== INFO: ts_open error\n");
        return;
    }
    if (ts_config(m_ts) != 0) {
        TOUCH_LOG("Init", 0x7e, " ==== HMI ==== INFO: ts_config error\n");
    }
}

// 0x00023aec VMultiTouch::Run
void VMultiTouch::Run() {
    Start();
    emit SigInitialize();
}

// 0x00023b04 VMultiTouch::SetMoveInterval (name is ours)
void VMultiTouch::SetMoveInterval(int ms) {
    m_tracker.move_interval_ms = ms;
}

// 0x00023dbc VMultiTouch::Initialize
// TODO(phase3): with m_raw_mode set (the default) stock also forwards every
// batch as VMULTITOUCH_RAW_DATA, to the projection proxy (0x003833fc) when
// screen 1 is up with its +0x5c flag, else through SigTouchData.
void VMultiTouch::Initialize() {
    if (m_ts == 0) {
        return;
    }
    m_clock.start();
    ts_sample samples[kMultiTouchSlots];
    bool logged_error = false;
    for (;;) {
        const int n = ts_read(m_ts, samples, kMultiTouchSlots);
        if (n < 0) {
            // Stock logs every failure; ours logs once per failure streak.
            if (!logged_error) {
                logged_error = true;
                TOUCH_LOG("Initialize", 0xcc, " ==== HMI ==== INFO: ts_read\n");
            }
            usleep(kReadErrorBackoffUs);
            continue;
        }
        logged_error = false;
        if (n == 0) {
            continue;
        }
        m_tracker.now_ms = m_clock.elapsed();
        VMULTITOUCH_DATA data;
        bool emit_data;
        if (!m_tracker.release_pending) {
            emit_data = UpdateMultiTouch(m_tracker, samples, n, &data);
        } else {
            emit_data = FlushMultiTouchRelease(m_tracker, samples, n, &data);
        }
        if (emit_data) {
            emit SigTouchEvent(data);
        }
    }
}
