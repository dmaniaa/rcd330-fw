// Stock source: control_logic/subsystem/system_time_calendar/vsystemtimecalendarproxy.cpp
#include "control_logic/subsystem/system_time_calendar/vsystemtimecalendarproxy.h"

#include "control_logic/subsystem/phase3_stubs.h"
#include "service/osal/osal_api.h"

namespace {

// 0x0096c79c kGroups
const uint8_t kGroups[1] = {0x70};

VSystemTimeCalendarProxy* g_systemTimeCalendarProxy = 0;

} // namespace

// TODO(phase3): stock member state.
// 0x00202190 VSystemTimeCalendarProxy::VSystemTimeCalendarProxy
VSystemTimeCalendarProxy::VSystemTimeCalendarProxy() : m_logged(false) {}

// 0x002021d0 VSystemTimeCalendarProxy::Instance
VSystemTimeCalendarProxy* VSystemTimeCalendarProxy::Instance() {
    if (g_systemTimeCalendarProxy == 0) {
        g_systemTimeCalendarProxy = new VSystemTimeCalendarProxy();
    }
    return g_systemTimeCalendarProxy;
}

// 0x00202218 VSystemTimeCalendarProxy::Init
void VSystemTimeCalendarProxy::Init() {
    RegisterVmfGroup(kGroups, sizeof(kGroups));
}

// TODO(phase3): port the stock handler.
// 0x00201fa4 VSystemTimeCalendarProxy::RecieveVmfMsg
void VSystemTimeCalendarProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VSystemTimeCalendarProxy", grp, sub);
}
