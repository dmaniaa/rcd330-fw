// Stock source: control_logic/subsystem/amfm/vamfmproxy.cpp
#include "control_logic/subsystem/amfm/vamfmproxy.h"

#include "control_logic/subsystem/phase3_stubs.h"
#include "service/osal/osal_api.h"

namespace {

// 0x00969978 kGroups
const uint8_t kGroups[1] = {0x6f};

VAMFMProxy* g_aMFMProxy = 0;

} // namespace

// TODO(phase3): stock member state.
// 0x000acfcc VAMFMProxy::VAMFMProxy
VAMFMProxy::VAMFMProxy() : m_logged(false) {}

// 0x000ad0a8 VAMFMProxy::Instance
VAMFMProxy* VAMFMProxy::Instance() {
    if (g_aMFMProxy == 0) {
        g_aMFMProxy = new VAMFMProxy();
    }
    return g_aMFMProxy;
}

// 0x000ad0f0 VAMFMProxy::Init
void VAMFMProxy::Init() {
    RegisterVmfGroup(kGroups, sizeof(kGroups));
}

// 0x000ad104 VAMFMProxy::OnGetCurrentInfoOpera
void VAMFMProxy::OnGetCurrentInfoOpera() {
    const uint8_t msg[5] = {0x8f, 0x22, 0x00, 0x01, 0x3d};
    log_simple("INFO", " ==== HMI ==== INFO: VAMFMProxy::OnGetCurrentInfoOpera", msg[4]);
    printlog(3, "INFO", "control_logic/subsystem/amfm/vamfmproxy.cpp", "OnGetCurrentInfoOpera", 0x1b4,
             " ==== HMI ==== INFO: VAMFMProxy::OnGetCurrentInfoOpera", msg[4]);
    SendVmfMsg(msg, sizeof(msg));
}

// 0x000ad1a4 VAMFMProxy::OnGetTunerListOpera
// Name is ours.
void VAMFMProxy::OnGetTunerListOpera() {
    const uint8_t msg[7] = {0x8f, 0x22, 0x00, 0x03, 0x3b, 0x00, 0x00};
    SendVmfMsg(msg, sizeof(msg));
}

// 0x000ae1f0 VAMFMProxy::OnGetSeekmodeOpera
void VAMFMProxy::OnGetSeekmodeOpera() {
    const uint8_t msg[7] = {0x8f, 0x22, 0x00, 0x03, 0x4e, 0x00, 0x00};
    SendVmfMsg(msg, sizeof(msg));
    log_simple("INFO", " ==== HMI ==== INFO: VAMFMProxy::OnGetSeekmodeOpera:pData[4]=%d", msg[4]);
    printlog(3, "INFO", "control_logic/subsystem/amfm/vamfmproxy.cpp", "OnGetSeekmodeOpera", 0x4b5,
             " ==== HMI ==== INFO: VAMFMProxy::OnGetSeekmodeOpera:pData[4]=%d", msg[4]);
}

// TODO(phase3): port the stock handler.
// 0x000ae28c VAMFMProxy::RecieveVmfMsg
void VAMFMProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VAMFMProxy", grp, sub);
}
