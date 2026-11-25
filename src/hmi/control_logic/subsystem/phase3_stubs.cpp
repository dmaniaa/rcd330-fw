// File name is ours; stock name unknown.
#include "control_logic/subsystem/phase3_stubs.h"

#include "service/osal/osal_api.h"

void ProxyLogUnhandledOnce(bool* logged, const char* proxy, uint8_t grp, uint8_t sub) {
    if (*logged) {
        return;
    }
    *logged = true;
    log_simple("INFO", " ==== HMI ==== INFO: %s: grp 0x%02x sub 0x%02x not handled in phase 2 (further messages dropped)\n",
               proxy, grp, sub);
}

namespace {

// 0x00969980 kBTProxyGroups
const uint8_t kBTProxyGroups[4] = {0x20, 0x1f, 0x27, 0x26};
VBTProxy* g_bTProxy = 0;

// 0x00969988 kMirrorLinkProxyGroups
const uint8_t kMirrorLinkProxyGroups[6] = {0x45, 0x7b, 0xc4, 0xd2, 0xc8, 0xc9};
VMirrorLinkProxy* g_mirrorLinkProxy = 0;

// 0x0096f224 kCarPlayProxyGroups
const uint8_t kCarPlayProxyGroups[3] = {0x81, 0xc3, 0x7b};
VCarPlayProxy* g_carPlayProxy = 0;

// 0x0096f4f8 kCarLifeProxyGroups
const uint8_t kCarLifeProxyGroups[4] = {0x3d, 0xc7, 0xc8, 0xc9};
VCarLifeProxy* g_carLifeProxy = 0;

// 0x0096f4fc kAndroidAutoProxyGroups
const uint8_t kAndroidAutoProxyGroups[7] = {0xdc, 0xc8, 0xc9, 0x20, 0x1f, 0x27, 0x26};
VAndroidAutoProxy* g_androidAutoProxy = 0;

// 0x0096c798 kServiceProxyGroups
const uint8_t kServiceProxyGroups[3] = {0xbe, 0x6f, 0x80};
VServiceProxy* g_serviceProxy = 0;

// 0x0096c794 kServiceAuxProxyGroups
const uint8_t kServiceAuxProxyGroups[2] = {0x1f, 0x70};
VServiceAuxProxy* g_serviceAuxProxy = 0;

// 0x00969964 kSubsystemProxyGroups
const uint8_t kSubsystemProxyGroups[1] = {0x70};
VSubsystemProxy* g_subsystemProxy = 0;

// 0x009701c8 kAppConnectPopupProxyGroups
const uint8_t kAppConnectPopupProxyGroups[2] = {0xc4, 0xd2};
VAppConnectPopupProxy* g_appConnectPopupProxy = 0;

} // namespace

// 0x000bf734 VBTProxy::Instance
VBTProxy* VBTProxy::Instance() {
    if (g_bTProxy == 0) {
        g_bTProxy = new VBTProxy();
    }
    return g_bTProxy;
}

// 0x000bf780 VBTProxy::Init
void VBTProxy::Init() {
    RegisterVmfGroup(kBTProxyGroups, sizeof(kBTProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x000cb140 VBTProxy::RecieveVmfMsg
void VBTProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VBTProxy", grp, sub);
}

// 0x000dc6d4 VMirrorLinkProxy::Instance
VMirrorLinkProxy* VMirrorLinkProxy::Instance() {
    if (g_mirrorLinkProxy == 0) {
        g_mirrorLinkProxy = new VMirrorLinkProxy();
    }
    return g_mirrorLinkProxy;
}

// 0x000dc520 VMirrorLinkProxy::Init
void VMirrorLinkProxy::Init() {
    RegisterVmfGroup(kMirrorLinkProxyGroups, sizeof(kMirrorLinkProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x000ddb0c VMirrorLinkProxy::RecieveVmfMsg
void VMirrorLinkProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VMirrorLinkProxy", grp, sub);
}

// 0x0032cd7c VCarPlayProxy::Instance
VCarPlayProxy* VCarPlayProxy::Instance() {
    if (g_carPlayProxy == 0) {
        g_carPlayProxy = new VCarPlayProxy();
    }
    return g_carPlayProxy;
}

// 0x0032cc70 VCarPlayProxy::Init
void VCarPlayProxy::Init() {
    RegisterVmfGroup(kCarPlayProxyGroups, sizeof(kCarPlayProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x003302f8 VCarPlayProxy::RecieveVmfMsg
void VCarPlayProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VCarPlayProxy", grp, sub);
}

// 0x0036b14c VCarLifeProxy::Instance
VCarLifeProxy* VCarLifeProxy::Instance() {
    if (g_carLifeProxy == 0) {
        g_carLifeProxy = new VCarLifeProxy();
    }
    return g_carLifeProxy;
}

// 0x0036b0a4 VCarLifeProxy::Init
void VCarLifeProxy::Init() {
    RegisterVmfGroup(kCarLifeProxyGroups, sizeof(kCarLifeProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x0036ca04 VCarLifeProxy::RecieveVmfMsg
void VCarLifeProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VCarLifeProxy", grp, sub);
}

// 0x00382e40 VAndroidAutoProxy::Instance
VAndroidAutoProxy* VAndroidAutoProxy::Instance() {
    if (g_androidAutoProxy == 0) {
        g_androidAutoProxy = new VAndroidAutoProxy();
    }
    return g_androidAutoProxy;
}

// 0x00382c90 VAndroidAutoProxy::Init
void VAndroidAutoProxy::Init() {
    RegisterVmfGroup(kAndroidAutoProxyGroups, sizeof(kAndroidAutoProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x003859b0 VAndroidAutoProxy::RecieveVmfMsg
void VAndroidAutoProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VAndroidAutoProxy", grp, sub);
}

// 0x00201694 VServiceProxy::Instance
VServiceProxy* VServiceProxy::Instance() {
    if (g_serviceProxy == 0) {
        g_serviceProxy = new VServiceProxy();
    }
    return g_serviceProxy;
}

// 0x002016e0 VServiceProxy::Init
void VServiceProxy::Init() {
    RegisterVmfGroup(kServiceProxyGroups, sizeof(kServiceProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x00201390 VServiceProxy::RecieveVmfMsg
void VServiceProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VServiceProxy", grp, sub);
}

// 0x00200ec8 VServiceAuxProxy::Instance
VServiceAuxProxy* VServiceAuxProxy::Instance() {
    if (g_serviceAuxProxy == 0) {
        g_serviceAuxProxy = new VServiceAuxProxy();
    }
    return g_serviceAuxProxy;
}

// 0x002012a4 VServiceAuxProxy::Init
void VServiceAuxProxy::Init() {
    RegisterVmfGroup(kServiceAuxProxyGroups, sizeof(kServiceAuxProxyGroups));
    // TODO(phase3): stock also resets a QString member (0x0020129c).
}

// TODO(phase3): port the stock handler.
// 0x00200d28 VServiceAuxProxy::RecieveVmfMsg
void VServiceAuxProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VServiceAuxProxy", grp, sub);
}

// 0x0005ee7c VSubsystemProxy::Instance
VSubsystemProxy* VSubsystemProxy::Instance() {
    if (g_subsystemProxy == 0) {
        g_subsystemProxy = new VSubsystemProxy();
    }
    return g_subsystemProxy;
}

// 0x0005eec4 VSubsystemProxy::Init
void VSubsystemProxy::Init() {
    RegisterVmfGroup(kSubsystemProxyGroups, sizeof(kSubsystemProxyGroups));
}

// TODO(phase3): port the stock handler.
// No origin tag: stock VSubsystemProxy::RecieveVmfMsg (0x0005f198, vtable
// slot 3 of 0x003f5590) is outside the decompiled function set.
void VSubsystemProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VSubsystemProxy", grp, sub);
}

// 0x003aba80 VAppConnectPopupProxy::Instance
VAppConnectPopupProxy* VAppConnectPopupProxy::Instance() {
    if (g_appConnectPopupProxy == 0) {
        g_appConnectPopupProxy = new VAppConnectPopupProxy();
    }
    return g_appConnectPopupProxy;
}

// 0x003aba40 VAppConnectPopupProxy::Init
void VAppConnectPopupProxy::Init() {
    RegisterVmfGroup(kAppConnectPopupProxyGroups, sizeof(kAppConnectPopupProxyGroups));
}

// TODO(phase3): port the stock handler.
// 0x003abb40 VAppConnectPopupProxy::RecieveVmfMsg
void VAppConnectPopupProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t*, uint16_t) {
    ProxyLogUnhandledOnce(&m_logged, "VAppConnectPopupProxy", grp, sub);
}
