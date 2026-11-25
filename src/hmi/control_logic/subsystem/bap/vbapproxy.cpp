// Stock source: control_logic/subsystem/bap/vbapproxy.cpp
#include "control_logic/subsystem/bap/vbapproxy.h"

#include "control_logic/subsystem/phase3_stubs.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "service/osal/osal_api.h"

namespace {

// 0x0096997c kBapGroups
const uint8_t kBapGroups[3] = {0x70, 0x7b, 0xa2};

const uint8_t kGrpBapEnv = 0xa2;
const uint8_t kEnvStatus = 0x05;
const uint8_t kCarInfo = 0x06;

// Sensor ids main() requests at boot, in stock order (0x000b70b0).
const uint8_t kBootSensorIds[10] = {0x40, 0x5c, 0x58, 0x59, 0x5a, 0x5b, 0x3c, 0x3d, 0x3e, 0x3f};

VBAPProxy* g_bapProxy = 0;

} // namespace

// 0x000af434 VBAPProxy::VBAPProxy
VBAPProxy::VBAPProxy() : m_env_p0(0), m_env_p6(0), m_country_valid(0), m_country(0), m_logged(false) {
    // TODO(phase3): the ~0x970-byte vehicle state block.
    m_env.temp_half_c = 0;
    m_env.unit_f = 0;
    m_env.invalid = true;
    m_env.night_mode = false;
}

// 0x000afb84 VBAPProxy::Instance
VBAPProxy* VBAPProxy::Instance() {
    if (g_bapProxy == 0) {
        g_bapProxy = new VBAPProxy();
    }
    return g_bapProxy;
}

// 0x000afbd0 VBAPProxy::Init
void VBAPProxy::Init() {
    RegisterVmfGroup(kBapGroups, sizeof(kBapGroups));
}

// 0x000b61ac VBAPProxy::OnSetBAPInfo
void VBAPProxy::OnSetBAPInfo(uint8_t id) {
    const bool sensor = (id >= 0x3c && id <= 0x40) || (id >= 0x58 && id <= 0x5c);
    if (!sensor) {
        return;
    }
    const uint8_t msg[6] = {0x8f, 0x24, 0x00, 0x02, id, 0x00};
    log_simple("INFO", " ==== HMI ==== INFO: VBAPProxy::OnSetBAPSensorSpace");
    printlog(3, "INFO", "control_logic/subsystem/bap/vbapproxy.cpp", "OnSetBAPInfo", 0x2482,
             " ==== HMI ==== INFO: VBAPProxy::OnSetBAPSensorSpace");
    SendVmfMsg(msg, sizeof(msg));
}

// 0x000b70b0 VBAPProxy::RequestSensorInfo
// Name is ours.
void VBAPProxy::RequestSensorInfo() {
    for (unsigned i = 0; i < sizeof(kBootSensorIds); ++i) {
        OnSetBAPInfo(kBootSensorIds[i]);
    }
}

// 0x000b1fd4 VBAPProxy::GetVehicleCountry
// Name is ours.
uint8_t VBAPProxy::GetVehicleCountry() const {
    return m_country_valid == 1 ? m_country : 0;
}

// 0x000b9970 VBAPProxy::RecieveVmfMsg
void VBAPProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) {
    if (data == 0) {
        return;
    }
    if (grp == kGrpBapEnv && sub == kEnvStatus) {
        BapExtTemp env = m_env;
        if (!DecodeBapEnvStatus(data, len, &env)) {
            log_simple("INFO", " ==== HMI ==== INFO: VBAPProxy: drop short 0xa2:0x05 (len %u)\n", len);
            return;
        }
        if (VPowerProxy::Instance()->GetPlatformType() != 1) {
            // Stock marks the temperature invalid (0xff) only on platform 1;
            // the others store pData[3..4] at +0x234/+0x235 instead.
            // TODO(phase3): platform 2 reads the temperature from +0x59e
            // (0x000b6cc8).
            env.invalid = false;
        }
        m_env = env;
        m_env_p0 = data[0];
        m_env_p6 = data[6];
        // TODO(phase3): night-mode changes go to CarPlay (0x0032ea34),
        // Android Auto (0x00384f00) and MirrorLink (0x000dd560).
        return;
    }
    if (grp == kGrpBapEnv && sub == kCarInfo && len >= 2) {
        m_country_valid = data[0];
        m_country = data[1];
        // TODO(phase3): with region 0xff and a valid country stock switches
        // the language here (0x508f0 per country), then the BEM battery
        // warnings, trunk / reverse state and the version strings.
        return;
    }
    // TODO(phase3): 0x70:0x01 vehicle status (~80 pData[0] cases), 0x7b,
    // 0xa2 subs 0..4 and 7.., and the power 0x0c:0x04 hook.
    ProxyLogUnhandledOnce(&m_logged, "VBAPProxy", grp, sub);
}
