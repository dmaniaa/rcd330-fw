// Stock source: control_logic/subsystem/diag/vdiagproxy.cpp
#include "control_logic/subsystem/diag/vdiagproxy.h"

#include <string.h>

#include "control_logic/subsystem/phase3_stubs.h"
#include "service/osal/osal_api.h"

namespace {

// 0x0096c790 kGroups
const uint8_t kGroups[2] = {0x73, 0xa1};

VDiagProxy* g_diagProxy = 0;

} // namespace

// TODO(phase3): stock member state.
// 0x001ff3f4 VDiagProxy::VDiagProxy
VDiagProxy::VDiagProxy() : m_logged(false) {}

// 0x001ffc10 VDiagProxy::Instance
VDiagProxy* VDiagProxy::Instance() {
    if (g_diagProxy == 0) {
        g_diagProxy = new VDiagProxy();
    }
    return g_diagProxy;
}

// 0x001fc6a4 VDiagProxy::Init
void VDiagProxy::Init() {
    RegisterVmfGroup(kGroups, sizeof(kGroups));
    NotifyDiagReady();
}

// 0x001fc340 VDiagProxy::OnResponesToDIAG
void VDiagProxy::OnResponesToDIAG(const uint8_t* buf, uint32_t len) {
    if (buf == 0 || len > 0x32) {
        log_simple("INFO", " ==== HMI ==== INFO: VDiagProxy: drop diag response (len %u)\n", len);
        return;
    }
    uint8_t msg[0x32];
    memset(msg, 0, sizeof(msg));
    memcpy(msg, buf, len);
    log_simple("INFO", " ==== HMI ==== INFO: VDiagProxy::OnResponesToDIAG %d %d %d %d %d %d %d %d %d", msg[0], msg[1],
               msg[2], msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    printlog(3, "INFO", "control_logic/subsystem/diag/vdiagproxy.cpp", "OnResponesToDIAG", 0x588,
             " ==== HMI ==== INFO: VDiagProxy::OnResponesToDIAG %d %d %d %d %d %d %d %d %d", msg[0], msg[1], msg[2],
             msg[3], msg[4], msg[5], msg[6], msg[7], msg[8]);
    SendVmfMsg(msg, len);
}

// 0x001fc650 VDiagProxy::NotifyDiagReady
// Name is ours.
void VDiagProxy::NotifyDiagReady() {
    const uint8_t msg[6] = {0xa0, 0x07, 0x00, 0x02, 0x73, 0x13};
    OnResponesToDIAG(msg, sizeof(msg));
}

// TODO(phase3): port the stock handler.
// 0x001fe10c VDiagProxy::RecieveVmfMsg
void VDiagProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VDiagProxy", grp, sub);
}
