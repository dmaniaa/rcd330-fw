// Stock source: control_logic/subsystem/appconnect/vappconnectproxy.cpp
#include "control_logic/subsystem/appconnect/vappconnectproxy.h"

#include "service/osal/osal_api.h"

namespace {

// 0x0096f228 kAppConnectGroups
const uint8_t kAppConnectGroups[4] = {0xc8, 0x72, 0xc9, 0x8d};

const uint8_t kGrpPss = 0x72;
const uint8_t kGrpPssAlt = 0x8d;
const uint8_t kPssReadReply = 0x04;

const char kFile[] = "control_logic/subsystem/appconnect/vappconnectproxy.cpp";

VAppConnectProxy* g_appConnectProxy = 0;

} // namespace

// 0x003482f8 VAppConnectProxy::VAppConnectProxy
VAppConnectProxy::VAppConnectProxy() : m_device_type(0), m_logged_phase3(false) {
    // TODO(phase3): device name strings, iOS / Android link state (+0x39e,
    // +0x39f = 0xff) and the 0x32-byte device table.
    m_coding.region_mode = 1;
    m_coding.sub_mode = 0;
    m_coding.cp = false;
    m_coding.aa = false;
    m_coding.ml = false;
    m_coding.cl = false;
}

// 0x0034860c VAppConnectProxy::Instance
VAppConnectProxy* VAppConnectProxy::Instance() {
    if (g_appConnectProxy == 0) {
        g_appConnectProxy = new VAppConnectProxy();
    }
    return g_appConnectProxy;
}

// 0x00346fb4 VAppConnectProxy::Init
void VAppConnectProxy::Init() {
    RegisterVmfGroup(kAppConnectGroups, sizeof(kAppConnectGroups));
}

// 0x003476a8 VAppConnectProxy::QueryPssCoding
// Name is ours.
void VAppConnectProxy::QueryPssCoding() {
    const uint8_t msg[7] = {0x8c, 0x08, 0x00, 0x03, 0x00, 0x72, 0x04};
    SendVmfMsg(msg, sizeof(msg));
}

// 0x00347224 VAppConnectProxy::IsCoded
bool VAppConnectProxy::IsCoded() const {
    uint8_t a = 0, b = 0, c = 0;
    if (m_coding.region_mode == 1) {
        GetAvailableApps(&a, &b, &c);
    } else if (m_coding.region_mode == 2) {
        GetFMAvailableAppRow(&a, &b, &c);
    }
    return a != 0 || b != 0 || c != 0;
}

// 0x00347498 VAppConnectProxy::GetRegionMode
int VAppConnectProxy::GetRegionMode() const {
    return static_cast<int>(m_coding.region_mode);
}

// 0x00347490 VAppConnectProxy::GetSubMode
// Name is ours.
int VAppConnectProxy::GetSubMode() const {
    return static_cast<int>(m_coding.sub_mode);
}

// 0x00347150 VAppConnectProxy::GetAvailableApps
void VAppConnectProxy::GetAvailableApps(uint8_t* cl, uint8_t* cp, uint8_t* ml) const {
    if (cl == 0 || cp == 0 || ml == 0) {
        return;
    }
    *cl = m_coding.cl;
    *cp = m_coding.cp;
    *ml = m_coding.ml;
}

// 0x00347180 VAppConnectProxy::GetFMAvailableAppRow
void VAppConnectProxy::GetFMAvailableAppRow(uint8_t* aa, uint8_t* cp, uint8_t* ml) const {
    if (aa == 0 || cp == 0 || ml == 0) {
        return;
    }
    *aa = m_coding.aa;
    *cp = m_coding.cp;
    *ml = m_coding.ml;
    log_simple("INFO", " ==== HMI ==== INFO: $$$$$$$$$$ GetFMAvailableAppRow *aa is %d,*cp is %d", *aa, *cp);
    printlog(3, "INFO", kFile, "GetFMAvailableAppRow", 0x441,
             " ==== HMI ==== INFO: $$$$$$$$$$ GetFMAvailableAppRow *aa is %d,*cp is %d", *aa, *cp);
}

// 0x00348700 VAppConnectProxy::IsAppDeviceConnected
// Name is ours. Only regions 1 and 2 have apps, and only a connected iOS
// (+0x1c == 2) or Android (3) phone counts.
bool VAppConnectProxy::IsAppDeviceConnected() const {
    const bool app_region = m_coding.region_mode == 1 || m_coding.region_mode == 2;
    if (!app_region || (m_device_type != 2 && m_device_type != 3)) {
        return false;
    }
    // TODO(phase3): the device type comes from connect_mgr messages (it stays
    // 0 until then); iOS then checks the CarPlay / CarLife links
    // (0x003473a0), Android the Android Auto / MirrorLink links (0x0034740c).
    return false;
}

// 0x0034ac5c VAppConnectProxy::RecieveVmfMsg
void VAppConnectProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) {
    if (data == 0) {
        return;
    }
    // DEVIATION: stock logs pData[0..3] whatever the length.
    const unsigned d0 = len > 0 ? data[0] : 0, d1 = len > 1 ? data[1] : 0;
    const unsigned d2 = len > 2 ? data[2] : 0, d3 = len > 3 ? data[3] : 0;
    log_simple("INFO",
               " ==== HMI ==== INFO: VAppConnectProxy::RecieveVmfMsg: group_id = %d, event_id = %d, pData[0] = %d, "
               "pData[1] = %d, pData[2] = %d  pData[3] = %d",
               grp, sub, d0, d1, d2, d3);
    printlog(3, "INFO", kFile, "RecieveVmfMsg", 0x55a,
             " ==== HMI ==== INFO: VAppConnectProxy::RecieveVmfMsg: group_id = %d, event_id = %d, pData[0] = %d, "
             "pData[1] = %d, pData[2] = %d  pData[3] = %d",
             grp, sub, d0, d1, d2, d3);
    if (grp == kGrpPss || grp == kGrpPssAlt) {
        if (sub != kPssReadReply) {
            return;
        }
        if (!DecodeAppConnectPss(data, len, &m_coding)) {
            log_simple("INFO", " ==== HMI ==== INFO: VAppConnectProxy: drop PSS coding reply, len %u selector %u\n",
                       len, d2);
            return;
        }
        // TODO(phase3): stock posts the reply to VDisplayTask (0x00063830).
        return;
    }
    if (!m_logged_phase3) {
        // TODO(phase3): connect_mgr groups 0xc8 / 0xc9 (device list, iOS link
        // steps, CarPlay / CarLife start).
        log_simple("INFO", " ==== HMI ==== INFO: VAppConnectProxy: group 0x%02x not handled in phase 2\n", grp);
        m_logged_phase3 = true;
    }
}
