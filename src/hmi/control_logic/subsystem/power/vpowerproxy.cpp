// Stock source: control_logic/subsystem/power/vpowerproxy.cpp
//
// Phase 2 ports the parts the boot / App-Connect screens need: the TX
// messages, the stored power state and the RUN path that brings the HMI up.
// The rest of RecieveVmfMsg (0x0005bab4 is ~2,300 lines of camera, phone,
// CarPlay and popup reactions) is marked TODO(phase3) where it sits.
#include "control_logic/subsystem/power/vpowerproxy.h"

#include "control_logic/subsystem/pss/vpssproxy.h"

#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/audio/vaudioproxy.h"
#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "control_logic/subsystem/proxy_decoders.h"
#include "service/osal/osal_api.h"

#define POWER_LOG(func, line, ...)                                                                   \
    do {                                                                                             \
        log_simple("INFO", __VA_ARGS__);                                                             \
        printlog(3, "INFO", "control_logic/subsystem/power/vpowerproxy.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

// 0x00969958 kPowerGroups
const uint8_t kPowerGroups[5] = {0x6e, 0x0d, 0x0e, 0x74, 0x0c};

const uint8_t kGrpPowerAgentOut = 0x0c;
const uint8_t kGrpPower0d = 0x0d;
const uint8_t kGrpPower0e = 0x0e;
const uint8_t kGrpHmiPowerIn = 0x6e;
const uint8_t kGrpVipHmi = 0x74;

const uint8_t kPaOutModeChg = 0x01;
const uint8_t kPaOutWakeUpReason = 0x02;
const uint8_t kPaOutDevSts = 0x04;
const uint8_t kPaOutVrmInfo = 0x06;
const uint8_t kPaOutTimeDisplay = 0x09;
const uint8_t kPaOutRvcCoded = 0x0d;
const uint8_t kWakeStateNotify = 200;

const uint8_t kPwrStateRun = 3;
const uint8_t kPwrStateTempOn = 6;
const uint8_t kPwrStateIgnOn = 0x0c;

const uint8_t kPopPageTempProtect = 0x6d;
const int kNoRecoverPage = 0xff;

class NullPowerNotifySink : public IPowerNotifySink {
public:
    virtual void PowersubsystemNotifyMsg(uint8_t, uint8_t, const uint8_t*, uint16_t) {}
};

NullPowerNotifySink g_nullPowerSink;
VPowerProxy* g_powerProxy = 0;

// 0x0096995e g_firstWakeReason
// Set until the first PA_OUT_WAKE_UP_REASON arrives. Name is ours.
bool g_firstWakeReason = true;

bool IsRunMode(uint8_t mode) {
    return mode == kPwrStateRun || mode == kPwrStateTempOn || mode == kPwrStateIgnOn;
}

} // namespace

// 0x00055d48 VPowerProxy::VPowerProxy
// Only the members phase 2 reads; stock zeroes ~160 bytes of state.
VPowerProxy::VPowerProxy()
    : m_notify_sink(&g_nullPowerSink),
      m_hmi_run(0),
      m_startup_state(0),
      m_power_state(0),
      m_run_state(0),
      m_grp0d_0(2),
      m_grp0d_1(0),
      m_aux_mode(0),
      m_running_init(0),
      m_audio_mode(0),
      m_acc_status(0),
      m_ignition_status(0),
      m_platform_type(0),
      m_platform_sub(0),
      m_backlight_on(0),
      m_vrm_info(0x11),
      m_rvc_coded(1),
      m_temp_protect(0),
      m_remote_wake(0),
      m_phone_wake(0),
      m_ign_mode_c(0),
      m_recover_page(kNoRecoverPage),
      m_first_wake_reason(0xff),
      m_wake_reason(0) {
    m_time_flags[0] = m_time_flags[1] = m_time_flags[2] = 0;
}

// 0x00055fd4 VPowerProxy::Instance
VPowerProxy* VPowerProxy::Instance() {
    if (g_powerProxy == 0) {
        g_powerProxy = new VPowerProxy();
    }
    return g_powerProxy;
}

// 0x0005601c VPowerProxy::Init
void VPowerProxy::Init() {
    RegisterVmfGroup(kPowerGroups, sizeof(kPowerGroups));
}

void VPowerProxy::SetPowerNotifySink(IPowerNotifySink* sink) {
    m_notify_sink = sink != 0 ? sink : &g_nullPowerSink;
}

// 0x00056040 VPowerProxy::SendWakeStateToPowerAgent
void VPowerProxy::SendWakeStateToPowerAgent(int wake_state) {
    const uint8_t msg[6] = {0x0b, 0x01, 0x00, 0x02, 0x6e, static_cast<uint8_t>(wake_state)};
    POWER_LOG("SendWakeStateToPowerAgent", 0xa53,
              " ==== HMI ==== INFO: VPowerProxy::SendWakeStateToPowerAgent wakeState=%d", wake_state);
    if (!SendVmfMsg(msg, sizeof(msg))) {
        // DEVIATION: stock logs only the line above, sent or not. The first
        // IgdtSwap usually reports wake state 2 before VmfThread::start()
        // has connected (handle -1); the 0x81 audio mode sends it again.
        log_simple("INFO", " ==== HMI ==== INFO: VPowerProxy::SendWakeStateToPowerAgent wakeState=%d not sent\n",
                   wake_state);
    }
}

// 0x000567a0 VPowerProxy::SetBacklightOnOFF
void VPowerProxy::SetBacklightOnOFF(uint8_t on) {
    uint8_t vip_state = 5;  // display off
    if (on == 1) {
        // While the over-temperature popup logic owns the display, only turn
        // it on when that popup is up.
        if (m_temp_protect != 0 && VDisplayProxy::Instance()->GetPopPageState(kPopPageTempProtect) != 1) {
            return;
        }
        vip_state = 4;  // display on
    }
    const uint8_t msg[5] = {0x8f, 0x27, 0x00, 0x01, vip_state};
    // STOCK-QUIRK: log_simple gets no argument for its %d.
    log_simple("INFO", " ==== HMI ==== INFO: VPowerProxy::onSendDisplayOnToVIP pData[5]=%d", vip_state);
    printlog(3, "INFO", "control_logic/subsystem/power/vpowerproxy.cpp", "SetBacklightOnOFF", 0xbb9,
             " ==== HMI ==== INFO: VPowerProxy::onSendDisplayOnToVIP pData[5]=%d", vip_state);
    SendVmfMsg(msg, sizeof(msg));
    m_backlight_on = on;
}

// 0x00056950 VPowerProxy::SetHMIOn
// STOCK-QUIRK: stock has no caller of SetHMIOn (no BL to it and no pointer
// to it in opt/hmi); RUN is handled inline in RecieveVmfMsg.
void VPowerProxy::SetHMIOn() {
    SetBacklightOnOFF(1);
    m_hmi_run = 1;
    VAudioProxy* audio = VAudioProxy::Instance();
    audio->OnBuildConnectionReq(audio->GetAudioMode());
    VDisplayProxy::Instance()->SetMainPage(audio->GetAudioMode(), 0);
    VDisplayProxy::Instance()->NotifyHmiOn();
    POWER_LOG("SetHMIOn", 0xc14, " ==== HMI ==== INFO: VPowerProxy::SetHMIOn ");
}

// 0x000568cc VPowerProxy::SetHMIOff
void VPowerProxy::SetHMIOff() {
    m_hmi_run = 0;
    SetBacklightOnOFF(0);
    VAudioProxy* audio = VAudioProxy::Instance();
    audio->OnOffConnectionReq(audio->GetAudioMode());
    POWER_LOG("SetHMIOff", 0xc04, " ==== HMI ==== INFO: VPowerProxy::SetHMIOff ");
}

// 0x00056a7c VPowerProxy::RestoreBacklight
// Name is ours.
void VPowerProxy::RestoreBacklight() {
    if (m_temp_protect != 0 && VDisplayProxy::Instance()->GetPopPageState(kPopPageTempProtect) != 1) {
        return;
    }
    SetBacklightOnOFF(1);
    // TODO(phase3): with the HMI running stock also refreshes popup 0x13
    // (0x0004ffa4).
}

// 0x0005b520 VPowerProxy::RecoverSetupMainPage
void VPowerProxy::RecoverSetupMainPage() {
    VDisplayProxy* display = VDisplayProxy::Instance();
    // TODO(phase3): stock first re-shows the call page when the phone-wake
    // flag (+0x91) is set and a call page is pending (0x00051818).
    const int main_page = display->GetMainPage();
    const int cur_page = display->GetCurDisplayPage();
    POWER_LOG("RecoverSetupMainPage", 0x1dac,
              " ==== HMI ==== INFO: =========wangqin cur main = %d, cur display = %d", main_page, cur_page);
    if (main_page == cur_page) {
        // TODO(phase3): stock re-inserts the page when the page cache
        // (0x00051c8c) and 0x00061220 say it was dropped; Android Auto (5)
        // gets its projection state refreshed.
        return;
    }
    const int audio_mode = VAudioProxy::Instance()->GetAudioMode();
    if ((audio_mode == 1 || audio_mode == 2) && main_page == 2) {
        display->InsertPage(1);  // radio audio on the media page -> Tuner
        return;
    }
    const bool media_audio = (audio_mode >= 3 && audio_mode <= 8) || audio_mode == 0x10;
    if (media_audio && main_page == 1) {
        display->InsertPage(2);  // media audio on the Tuner page -> Media
        return;
    }
    if (main_page == 5 || main_page == 6 || main_page == 7 || main_page == 10) {
        // TODO(phase3): Android Auto / CarLife / CarPlay / MirrorLink pages
        // depend on their projection session state.
        return;
    }
    display->InsertPage(main_page);
}

// 0x0005a0a0 VPowerProxy::GetPlatformType
uint8_t VPowerProxy::GetPlatformType() const {
    return m_platform_type;
}

// 0x0005a0b8 VPowerProxy::IsHMIOn
uint8_t VPowerProxy::IsHMIOn() const {
    return m_backlight_on;
}

// 0x0005a394 VPowerProxy::SetRvcCoded
void VPowerProxy::SetRvcCoded(uint8_t coded) {
    m_rvc_coded = coded;
}

// 0x0005a3ec VPowerProxy::SetStartupDone
void VPowerProxy::SetStartupDone(uint8_t state) {
    m_startup_state = state;
}

// 0x000563bc VPowerProxy::GetAuxMode
uint8_t VPowerProxy::GetAuxMode() const {
    if (VAppConnectProxy::Instance()->GetRegionMode() == 2 && m_platform_type == 1) {
        return 1;
    }
    return m_aux_mode;
}

// 0x0005a068 VPowerProxy::GetPowerState
uint8_t VPowerProxy::GetPowerState() const {
    return m_power_state;
}

// 0x0005a070 VPowerProxy::GetTimeDisplayFlags
void VPowerProxy::GetTimeDisplayFlags(uint8_t* f0, uint8_t* f1, uint8_t* f2) const {
    if (f0 == 0 || f1 == 0 || f2 == 0) {
        return;
    }
    *f0 = m_time_flags[0];
    *f1 = m_time_flags[1];
    *f2 = m_time_flags[2];
}

// 0x0005b510 VPowerProxy::GetRecoverPage
// Name is ours.
int VPowerProxy::GetRecoverPage() const {
    return m_recover_page;
}

// 0x0005a88c VPowerProxy::GetPhoneWakeFlag
// Name is ours.
uint8_t VPowerProxy::GetPhoneWakeFlag() const {
    return m_phone_wake;
}

// 0x0005a850 VPowerProxy::GetRemoteWakeFlag
// Name is ours.
uint8_t VPowerProxy::GetRemoteWakeFlag() const {
    return m_remote_wake;
}

// 0x0005b9cc VPowerProxy::GetTempProtect
// Name is ours.
uint8_t VPowerProxy::GetTempProtect() const {
    return m_temp_protect;
}

// 0x000561ec VPowerProxy::GetPwrCurStateToPowerAgent
void VPowerProxy::GetPwrCurStateToPowerAgent() {
    const uint8_t msg[7] = {0x0b, 0x03, 0x00, 0x03, 0x0c, 0x01, 0x02};
    POWER_LOG("GetPwrCurStateToPowerAgent", 0xa94, " ==== HMI ==== INFO: VPowerProxy::GetPwrCurStateToPowerAgent ");
    SendVmfMsg(msg, sizeof(msg));
}

// Part of 0x0005bab4: the RUN / TEMP_ON / IGN branch of PA_OUT_MODE_CHG.
bool VPowerProxy::HandleRunMode(uint8_t mode) {
    m_run_state = mode;
    // TODO(phase3): BAP/clock refresh when the climate popup is up (0x000b8208).
    m_ign_mode_c = mode == kPwrStateIgnOn;
    PowerRunFlags flags = {m_startup_state, m_hmi_run, m_running_init};
    const PowerRunAction action = EvaluatePowerRunMode(&flags, VAudioProxy::Instance()->GetAudioMode());
    m_hmi_run = flags.hmi_run;
    m_running_init = flags.running_init;
    // TODO(phase3): RUN <-> TEMP_ON <-> IGN switches re-check phone /
    // CarPlay call pages (0x0005d7a4..0x0005d7e4).
    if (action != kPowerRunShowMainPage) {
        // No audio mode from the VIP yet: the 0x81 audio mode sets it and
        // asks pwr_agent for the power state again (GetPwrCurStateToPowerAgent),
        // whose RUN reply comes back here.
        return false;
    }
    // TODO(phase3): App-Connect region 2 page refresh (0x00054038), BT /
    // CarPlay call recovery and the rear-view camera checks
    // (0x0005c1f8..0x0005c2c0) come here in stock.
    VAudioProxy* audio = VAudioProxy::Instance();
    const uint8_t audio_mode = audio->GetAudioMode();
    if (audio_mode == 0x0c) {
        // TODO(phase3): CarPlay audio re-selects its channel by session state.
    } else {
        audio->OnBuildConnectionReq(audio_mode);
    }
    // TODO(phase3): stock picks App-Connect / MirrorLink / Android Auto pages
    // (0x0005c300..0x0005c340) for main pages 5..10.
    RecoverSetupMainPage();
    VDisplayProxy::Instance()->UpdateBacklight();
    SetBacklightOnOFF(1);  // 0x00056ad4 is only this call
    // TODO(phase3): pending popup (VDisplayProxy +0xd4) and BAP 0x000b4ecc.
    return true;
}

// Part of 0x0005bab4: PA_OUT_MODE_CHG.
bool VPowerProxy::HandleModeChange(const uint8_t* data, uint16_t len) {
    uint8_t mode = 0;
    if (!DecodePowerModeChange(data, len, &mode)) {
        return true;
    }
    POWER_LOG("RecieveVmfMsg", 0x248, " ==== HMI ==== INFO: VPowerProxy::POWER_AGENT_OUT->PA_OUT_MODE_CHG %d", mode);
    m_power_state = mode;
    // TODO(phase3): BAP / status-line refresh (0x000b7710, 0x00059f34..
    // 0x0005a168), CarPlay / CarLife / Android Auto standby messages
    // (0x0005ba34) and the climate popup close.
    if (IsRunMode(mode)) {
        return HandleRunMode(mode);
    }
    // TODO(phase3): STANDBY (2), USER_OFF (7), OFF (8/9), DEMO (10) and
    // ONE_MINUTE (11) close pages, mute and dim the display.
    return true;
}

// 0x0005bab4 VPowerProxy::RecieveVmfMsg
void VPowerProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) {
    if (data == 0) {
        return;
    }
    if (grp == kGrpPowerAgentOut) {
        if (sub == kPaOutModeChg) {
            if (!HandleModeChange(data, len)) {
                return;
            }
        } else if (sub == kPaOutWakeUpReason && len >= 2) {
            POWER_LOG("RecieveVmfMsg", 0x664, " ==== HMI ==== INFO: VPowerProxy::PA_OUT_WAKE_UP_REASON %d, %d", data[0],
                      data[1]);
            if (g_firstWakeReason) {
                m_first_wake_reason = data[1];
                g_firstWakeReason = false;
            }
            // TODO(phase3): eject-key and phone wake (8/9) set the call-page
            // recovery flag (+0x91).
            m_wake_reason = data[0];
        } else if (sub == kPaOutDevSts) {
            PowerDevStatus dev;
            if (DecodePowerDevStatus(data, len, &dev)) {
                // TODO(phase3): ignition / ACC edges drive the phone reminder,
                // clock and camera pages (0x0005c7b4..0x0005c7ec).
                m_ignition_status = dev.ignition_status;
                m_acc_status = dev.acc_status;
                POWER_LOG("RecieveVmfMsg", 0x73d,
                          " ==== HMI ==== INFO: ======VPowerProxy::PA_OUT_DEV_STS m_ucIgnitionStatus=%d "
                          "m_ucACCStatus=%d ",
                          m_ignition_status, m_acc_status);
                if (len >= 2) {
                    const bool row_lowline =
                        VAppConnectProxy::Instance()->GetRegionMode() == 2 && m_platform_type == 2;
                    m_aux_mode = row_lowline ? 0 : static_cast<uint8_t>((data[1] & 0x0c) >> 2);
                }
            }
        } else if (sub == kPaOutVrmInfo && len >= 3) {
            POWER_LOG("RecieveVmfMsg", 0x836,
                      " ==== HMI ==== INFO: ======VPowerProxy::PA_OUT_VRM_INTO m_platform_type =%d", data[0]);
            // A platform change re-reads the system setup block, whose read
            // reply decides region and language (VPSSProxy 0x001c5a90).
            // TODO(phase3): the 11-byte part number at pData[4] selects the
            // 5JF035187 diag mode.
            if (m_platform_type != data[0]) {
                VPSSProxy::Instance()->OnSendPSSReadSystemSetupStatusRequest();
            }
            m_platform_type = data[0];
            m_platform_sub = data[1];
            m_vrm_info = data[2];
        } else if (sub == kPaOutTimeDisplay && len >= 3) {
            // TODO(phase3): turning the clock flag on while the BT page and
            // standby are up shows the clock page (0x0005c560..0x0005c8c0).
            m_time_flags[0] = data[0];
            m_time_flags[1] = data[1];
            m_time_flags[2] = data[2];
        } else if (sub == kPaOutRvcCoded && len >= 2) {
            m_rvc_coded = data[1] == 1;
        }
        // TODO(phase3): temperature (7), voltage (8), net info (0x10), ...
    } else if (grp == kGrpPower0d) {
        if (sub == 0 && len >= 2) {
            m_grp0d_1 = data[0];
            m_grp0d_0 = data[1];
        } else if (sub == 2 && len >= 1) {
            m_grp0d_1 = data[0];
            m_grp0d_0 = data[0];
        }
    } else if (grp == kGrpPower0e) {
        // Forwarded unchanged.
    } else if (grp == kGrpHmiPowerIn) {
        if (sub == kWakeStateNotify && len >= 1) {
            PowerRunFlags flags = {m_startup_state, m_hmi_run, m_running_init};
            const bool running_cascade = ApplyPowerWakeState(&flags, data[0]);
            m_startup_state = flags.startup_state;
            m_running_init = flags.running_init;
            if (data[0] == kWakeStateIdle) {
                POWER_LOG("RecieveVmfMsg", 0x13d, " ==== HMI ==== INFO: -------VPowerProxy::WS_IDLE 555555555555");
                // TODO(phase3): close the climate / standby popups and stop
                // the BAP clock (0x000b6714, 0x000b2310).
                SendWakeStateToPowerAgent(1);
            } else if (data[0] == kWakeStateRunning) {
                // TODO(phase3): the rest of the WS_RUNNING cascade re-reads
                // PSS, media, diag and BT state (0x0005c040..0x0005c0b8).
                if (running_cascade) {
                    // Re-read the App-Connect coding: diag writes it to PSS
                    // during boot, possibly after the query from main().
                    VAppConnectProxy::Instance()->QueryPssCoding();
                    // The VIP's 0x81 answer re-runs the start-up handshake
                    // (VControlTask 0x00034630 -> GetPwrCurStateToPowerAgent),
                    // and its RUN reply sets hmi_run again. Without it a
                    // wake-state cycle after the boot page (our hmi starts
                    // faster than stock) left hmi_run 0 and every key dropped.
                    VAudioProxy::Instance()->OnGetAudioModeFromVIP();
                }
                POWER_LOG("RecieveVmfMsg", 0x1b3, " ==== HMI ==== INFO: -------VPowerProxy::WS_RUNNING 6666666");
            }
            m_hmi_run = flags.hmi_run;
            VDisplayProxy::Instance()->UpdateBacklight();
        }
        // TODO(phase3): sub 1 (clock), 2, 3, 4 (system time), 0xd9.
    } else if (grp == kGrpVipHmi) {
        if (sub == 1 && len >= 2) {
            uint8_t audio_mode = 0;
            if (DecodeVipAudioMode(data, len, &audio_mode)) {
                m_audio_mode = audio_mode;
                POWER_LOG("RecieveVmfMsg", 0x8c4, " ==== HMI ==== INFO: MSG_VIPHMI_APHMI_AUDIO_MODE =%d", audio_mode);
            } else if (data[0] == 0x82) {
                const bool row_lowline = VAppConnectProxy::Instance()->GetRegionMode() == 2 && m_platform_type == 2;
                m_aux_mode = (!row_lowline && data[1] == 1) ? 1 : 0;
                POWER_LOG("RecieveVmfMsg", 0x8bf, " ==== HMI ==== INFO: MSG_VIPHMI_APHMI_AUX_STATE =%d", data[1]);
            }
            // TODO(phase3): 0x83 / 0x8c steering-wheel keys, 0x84..0x90.
        }
    } else {
        return;  // stock: other groups are not forwarded
    }
    m_notify_sink->PowersubsystemNotifyMsg(grp, sub, data, len);
    // TODO(phase3): stock also posts the message to VDisplayTask (0x00063830).
}
