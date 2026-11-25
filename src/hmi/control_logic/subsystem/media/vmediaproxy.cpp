// Stock source: control_logic/subsystem/media/vmediaproxy.cpp
#include "control_logic/subsystem/media/vmediaproxy.h"

#include "control_logic/subsystem/phase3_stubs.h"
#include "service/osal/osal_api.h"

namespace {

// 0x00969984 kGroups
const uint8_t kGroups[4] = {0x2a, 0x20, 0x1f, 0x70};

VMediaProxy* g_mediaProxy = 0;

} // namespace

// TODO(phase3): stock member state.
// 0x000d7f74 VMediaProxy::VMediaProxy
VMediaProxy::VMediaProxy() : m_logged(false) {}

// 0x000d82e0 VMediaProxy::Instance
VMediaProxy* VMediaProxy::Instance() {
    if (g_mediaProxy == 0) {
        g_mediaProxy = new VMediaProxy();
    }
    return g_mediaProxy;
}

// 0x000d59d0 VMediaProxy::Init
void VMediaProxy::Init() {
    RegisterVmfGroup(kGroups, sizeof(kGroups));
}

// 0x000d6fe0 VMediaProxy::OnGetMsg
void VMediaProxy::OnGetMsg(uint8_t state) {
    const uint8_t msg[8] = {0x28, 0x00, 0x00, 0x04, 0x17, 0x2a, 0x01, state};
    SendVmfMsg(msg, sizeof(msg));
    log_simple("INFO", " ==== HMI ==== INFO: VMediaProxy::OnGetMsg MSG_MEDIA_APP_DEVICE_STATE_RET %d", state);
    printlog(3, "INFO", "control_logic/subsystem/media/vmediaproxy.cpp", "OnGetMsg", 0xbe6,
             " ==== HMI ==== INFO: VMediaProxy::OnGetMsg MSG_MEDIA_APP_DEVICE_STATE_RET %d", state);
}

// TODO(phase3): port the stock handler.
// 0x000d9360 VMediaProxy::RecieveVmfMsg
void VMediaProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VMediaProxy", grp, sub);
}
