// Stock source: control_logic/input/vkeydriver.cpp
#include "control_logic/input/vkeydriver.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <QtCore/QTimer>

#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "control_logic/vcontroltask.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "service/osal/osal_api.h"

#define KEY_LOG(func, line, ...)                                                          \
    do {                                                                                  \
        log_simple("FOR_FREEZE", __VA_ARGS__);                                            \
        printlog(3, "FOR_FREEZE", "control_logic/input/vkeydriver.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

// 0x00976c90 g_keyDriver (set by the constructor, read by Instance).
VKeyDriver* g_keyDriver = 0;

const char kKeyDevice[] = "/dev/event0";
const int kFirstReadDelayMs = 500;

}  // namespace

// 0x000aa5bc VKeyDriver::VKeyDriver
VKeyDriver::VKeyDriver() : m_fd(-1), m_logged_short(false) {
    m_state.active_code = 0;
    m_state.multi_permit = true;
    m_state.tune_ok_down = false;
    g_keyDriver = this;
}

// 0x000aa64c VKeyDriver::Instance
VKeyDriver* VKeyDriver::Instance() {
    return g_keyDriver;
}

// 0x000aa664 VKeyDriver::RegisterSignals (name is ours)
void VKeyDriver::RegisterSignals() {
    connect(this, SIGNAL(SigInitialize()), this, SLOT(InitializeSlot()));
    connect(this, SIGNAL(switchToRead()), this, SLOT(readKeyDriver()), Qt::QueuedConnection);
}

// 0x000aa6bc VKeyDriver::Init
void VKeyDriver::Init() {
    RegisterSignals();
    emit SigInitialize();
}

// 0x000aa6d4 VKeyDriver::InitializeSlot
// Stock opens O_RDWR and starts reading even when open fails (the first
// read then fails and ends the loop).
void VKeyDriver::InitializeSlot() {
    m_fd = open(kKeyDevice, O_RDWR);
    if (m_fd < 0) {
        // Ours: stock does not log the failed open.
        KEY_LOG("InitializeSlot", 0, " ==== HMI ==== ERROR: open %s failed: %s; no hard keys", kKeyDevice,
                strerror(errno));
        return;
    }
    QTimer::singleShot(kFirstReadDelayMs, this, SLOT(readKeyDriver()));
}

// 0x000aa70c VKeyDriver::IsTuneOkDown
bool VKeyDriver::IsTuneOkDown() const {
    return m_state.tune_ok_down;
}

// 0x000aa714 VKeyDriver::SetKeyCode (name is ours)
void VKeyDriver::SetKeyCode(uint16_t code) {
    m_state.active_code = code;
}

uint16_t VKeyDriver::GetKeyCode() const {
    return m_state.active_code;
}

// 0x000aa77c VKeyDriver::readKeyDriver
// TODO(phase3): the VDiagProxy key reports after the decision
// (0x001ff158 on platform 2, 0x001fe094, 0x001fc48c).
void VKeyDriver::readKeyDriver() {
    InputEvent32 ev;
    const ReadResult res = ReadInputEvent(m_fd, &ev);
    if (res == kReadError || res == kReadEof) {
        // Stock stops reading on -1 without a log. DEVIATION: EOF also stops
        // (stock would reprocess the previous record forever).
        KEY_LOG("readKeyDriver", 0, " ==== HMI ==== ERROR: read %s failed (%s); hard keys stopped", kKeyDevice,
                res == kReadEof ? "EOF" : strerror(errno));
        return;
    }
    if (res == kReadShort) {
        // DEVIATION: stock processes a partial record; ours drops it.
        if (!m_logged_short) {
            m_logged_short = true;
            KEY_LOG("readKeyDriver", 0, " ==== HMI ==== WARN: short read on %s dropped (logged once)", kKeyDevice);
        }
        emit switchToRead();
        return;
    }
    if (ev.type != kEvKey || ev.code > kHardKeyMax) {
        emit switchToRead();
        return;
    }
    KEY_LOG("readKeyDriver", 0xc9,
            " ==== HMI ==== FOR_FREEZE: 533269 readKeyDriver  hmi_wrapper_igdt_get_hardkey_permit = %d",
            hmi_wrapper_igdt_get_hardkey_permit());
    KEY_LOG("readKeyDriver", 0xca,
            " ==== HMI ==== FOR_FREEZE: 533269 readKeyDriver  hmi_wrapper_igdt_get_mutihardkey_permit = %d",
            hmi_wrapper_igdt_get_mutihardkey_permit());
    KEY_LOG("readKeyDriver", 0xcb, " ==== HMI ==== FOR_FREEZE: 533269 readKeyDriver  ev_key.value = %d", ev.value);

    const uint16_t code = RemapHardKeyCode(ev.code, VPowerProxy::Instance()->GetPlatformType(),
                                           VAppConnectProxy::Instance()->GetRegionMode());
    m_state.multi_permit = hmi_wrapper_igdt_get_mutihardkey_permit();
    const uint16_t prev_code = m_state.active_code;
    uint8_t out_code = 0;
    uint8_t out_val = 0;
    bool toggle = false;
    const bool post = DecodeHardKeyEvent(m_state, hmi_wrapper_igdt_get_hardkey_permit(), ev.type, code, ev.value,
                                         &out_code, &out_val, &toggle);
    if (toggle) {
        if (m_state.multi_permit) {
            hmi_wrapper_igdt_set_mutihardkey_permit(true);
            hmi_wrapper_igdt_set_touch_permit(true);
        } else {
            hmi_wrapper_igdt_set_touch_permit(false);
            hmi_wrapper_igdt_set_mutihardkey_permit(false);
        }
    }
    if (hmi_wrapper_igdt_get_hardkey_permit() && !m_state.multi_permit && !toggle && code != prev_code) {
        KEY_LOG("readKeyDriver", 0xf3,
                " ==== HMI ==== FOR_FREEZE: 533269 readKeyDriver  ev_key.code != keycode_bak, keycode_bak = %d",
                prev_code);
    }
    if (post) {
        VControlTask* control = VControlTask::Instance();
        if (control != 0) {
            control->PostKeyEvent(out_code, out_val);
        }
    }
    emit switchToRead();
}
