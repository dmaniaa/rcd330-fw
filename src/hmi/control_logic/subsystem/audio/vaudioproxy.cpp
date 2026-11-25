// Stock source: control_logic/subsystem/audio/vaudioproxy.cpp
#include "control_logic/subsystem/audio/vaudioproxy.h"

#include "control_logic/subsystem/power/vpowerproxy.h"
#include "service/osal/osal_api.h"

#define AUDIO_LOG(func, line, ...)                                                                   \
    do {                                                                                             \
        log_simple("INFO", __VA_ARGS__);                                                             \
        printlog(3, "INFO", "control_logic/subsystem/audio/vaudioproxy.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

// 0x00967984 kAudioGroups
const uint8_t kAudioGroups[3] = {0x3d, 0x3f, 0x45};

const uint8_t kGrpAudioCm = 0x3d;
const uint8_t kGrpAudioAm = 0x3f;
const uint8_t kConnectResult = 0xc1;
const uint8_t kAmPrimaryVolume = 0x00;
const uint8_t kAmHmiMute = 0x04;

VAudioProxy* g_audioProxy = 0;

// Phone / BT call channels, which only take volume and mute updates.
bool IsCallChannel(uint8_t ch) {
    return ch == 9 || ch == 0x0e || ch == 0x11;
}

} // namespace

// 0x00046a9c VAudioProxy::VAudioProxy
VAudioProxy::VAudioProxy()
    : m_connect_result(0), m_cur_channel(0), m_target_channel(1), m_prev_channel(0), m_channel_type(0), m_mute_p4(0) {
    // TODO(phase3): the audio mode priority queue (0x000368ac, 0x00036c10).
    m_volume.active = 0;
    m_volume.primary_p2 = 0;
    m_volume.primary_p3 = 0;
    m_volume.mute_p2 = 0;
    m_volume.mute_p3 = 0;
    m_volume.muted = false;
}

// 0x00046b38 VAudioProxy::Instance
VAudioProxy* VAudioProxy::Instance() {
    if (g_audioProxy == 0) {
        g_audioProxy = new VAudioProxy();
    }
    return g_audioProxy;
}

// 0x00046b80 VAudioProxy::Init
void VAudioProxy::Init() {
    RegisterVmfGroup(kAudioGroups, sizeof(kAudioGroups));
    // TODO(phase3): 17 channel registrations (0x000467c4) and the 2-slot
    // queue at +0x10.
    m_volume.active = 10;
    m_volume.muted = false;
    m_volume.mute_p2 = 0;
    m_volume.mute_p3 = 0;
    m_mute_p4 = 0;
    m_connect_result = 0;
    m_channel_type = 0;
}

// 0x00046fb8 VAudioProxy::GetAudioMode
uint8_t VAudioProxy::GetAudioMode() const {
    return m_cur_channel;
}

// 0x00046f10 VAudioProxy::SetCurPrimaryAudioChannel
void VAudioProxy::SetCurPrimaryAudioChannel(uint8_t channel) {
    if (m_cur_channel == channel) {
        return;
    }
    m_prev_channel = m_cur_channel;
    m_cur_channel = channel;
    AUDIO_LOG("SetCurPrimaryAudioChannel", 0x66c,
              " ==== HMI ==== INFO: VAudioProxy::SetCurPrimaryAudioChannel: m_primary_audio_cur_channel=%2d", channel);
    // TODO(phase3): media source bookkeeping (+0x14/+0x44/+0x4c, 0x000d7120).
}

// 0x000470ac VAudioProxy::SetTargetPrimaryAudioChannel
void VAudioProxy::SetTargetPrimaryAudioChannel(int channel) {
    if (channel != 0) {
        m_target_channel = static_cast<uint8_t>(channel);
    }
    AUDIO_LOG("SetTargetPrimaryAudioChannel", 0x6e6,
              " ==== HMI ==== INFO: VAudioProxy::SetTargetPrimaryAudioChannel! %d", channel);
}

// 0x0004875c VAudioProxy::InsertAudioMode
void VAudioProxy::InsertAudioMode(uint8_t audio_id) {
    AUDIO_LOG("InsertAudioMode", 0x1bd, " ==== HMI ==== INFO:  VAudioProxy::InsertAudioMode audio_id=%d", audio_id);
    if (VPowerProxy::Instance()->GetStartupState() == 0) {
        return;
    }
    if (audio_id == m_cur_channel && (audio_id < 10 || audio_id > 0x0d)) {
        return;
    }
    const bool known = (audio_id >= 1 && audio_id <= 0x11) && audio_id != 0x0f;
    if (!known) {
        return;  // stock logs "relash" with TargetMode 0 and changes nothing
    }
    AUDIO_LOG("InsertAudioMode", 0x226, " ==== HMI ==== INFO: VAudioProxy::InsertAudioMode TargetMode=%d", audio_id);
    // DEVIATION: stock queues the mode in its priority queue (0x00037094,
    // 0x00038610) and tells CarPlay / CarLife (0x0032df24, 0x0036bec8);
    // phase 2 has neither, so the mode becomes current right away.
    // TODO(phase3): port the audio priority queue.
    SetCurPrimaryAudioChannel(audio_id);
    OnBuildConnectionReq(audio_id);
}

// 0x000467f0 VAudioProxy::channel_switch_out
uint8_t VAudioProxy::channel_switch_out(int audio_mode) {
    AUDIO_LOG("channel_switch_out", 0x4e, " ==== HMI ==== INFO: channel_switch_out:", audio_mode);
    uint8_t ch = 0;
    switch (audio_mode) {
    case 1: ch = 0x01; break;
    case 2: ch = 0x02; break;
    case 3: ch = 0x07; break;
    case 4: ch = 0x03; break;
    case 5: ch = 0x04; break;
    case 6: ch = 0x06; break;
    case 7: ch = 0x05; break;
    case 8: ch = 0x08; break;
    case 9: ch = 0x09; break;
    // TODO(phase3): 10..13 depend on the Android Auto / CarLife / CarPlay /
    // MirrorLink session state; these are the stock idle-session values.
    case 10: ch = 0x14; break;
    case 11: ch = 0x12; break;
    case 12: ch = 0x0e; break;
    case 13: ch = 0x0c; break;
    case 0x0e: ch = 0x0b; break;
    case 0x10: ch = 0x0d; break;
    case 0x11: ch = 0x0a; break;
    default: ch = 0; break;
    }
    // TODO(phase3): 9/0x0e/0x11 stop a running ringtone (0x003b34e4), and
    // leaving 0x11 resets the MLC call state (0x000ecfac).
    m_channel_type = ch;
    AUDIO_LOG("channel_switch_out", 0xf3, " ==== HMI ==== INFO: channel_switch channel_type=:", audio_mode);
    return ch;
}

// 0x00048240 VAudioProxy::OnBuildConnectionReq
int VAudioProxy::OnBuildConnectionReq(int audio_mode) {
    // TODO(phase3): with the status line up, some channels reset the
    // speech / phone volume first (0x0004800c, 0x00048184).
    const uint8_t ch = channel_switch_out(audio_mode);
    const uint8_t msg[7] = {0x3c, 0x00, 0x00, 0x03, 0xc2, 0x3d, ch};
    AUDIO_LOG("OnBuildConnectionReq", 0x91e, " ==== HMI ==== INFO: VAudioProxy::OnBuildConnectionReq pData[6]=%d", ch);
    if (VPowerProxy::Instance()->GetHmiRunFlag() == 1) {
        SendVmfMsg(msg, sizeof(msg));
        AUDIO_LOG("OnBuildConnectionReq", 0x923,
                  " ==== HMI ==== INFO: VAudioProxy::SendVmfMsg AUDIO_CM_BUILD_SOURCE_CONNECT_REQUEST", ch);
        m_connect_result = 0;
    }
    SetTargetPrimaryAudioChannel(audio_mode);
    // TODO(phase3): OnSaveAudioModeToVIP (0x00047810) and the VSubsystemProxy /
    // media notifications that follow it.
    return 1;
}

// 0x000476ac VAudioProxy::OnOffConnectionReq
int VAudioProxy::OnOffConnectionReq(int audio_mode) {
    const uint8_t ch = channel_switch_out(audio_mode);
    const uint8_t msg[7] = {0x3c, 0x00, 0x00, 0x03, 0xc4, 0x3d, ch};
    AUDIO_LOG("OnOffConnectionReq", 0x96b, " ==== HMI ==== INFO: VAudioProxy::OnOffConnectionReq pData[6]=%d", ch);
    SendVmfMsg(msg, sizeof(msg));
    return 1;
}

// 0x00049da8 VAudioProxy::OnGetAllAudioSetting
void VAudioProxy::OnGetAllAudioSetting() {
    const uint8_t msg[6] = {0x3e, 0x00, 0x00, 0x02, 0x19, 0x3f};
    AUDIO_LOG("OnGetAllAudioSetting", 0x1151, " ==== HMI ==== INFO: VAudioProxy::OnGetAllAudioSetting %d,%d,%d,%d",
              msg[0], msg[1], msg[4], msg[5]);
    SendVmfMsg(msg, sizeof(msg));
}

// 0x0004776c VAudioProxy::OnGetAudioModeFromVIP
void VAudioProxy::OnGetAudioModeFromVIP() {
    AUDIO_LOG("OnGetAudioModeFromVIP", 0x9bf, " ==== HMI ==== INFO: VAudioProxy::OnGetAudioModeFromVIP pData[5]=%d",
              kGetAudioModeFromVipMsg[4]);
    SendVmfMsg(kGetAudioModeFromVipMsg, sizeof(kGetAudioModeFromVipMsg));
}

// 0x00049f24 VAudioProxy::RecieveVmfMsg
void VAudioProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) {
    (void)sub;  // stock dispatches group 0x3f on pData[0] only
    if (data == 0 || len < 1) {
        return;
    }
    if (grp == kGrpAudioCm) {
        if (data[0] == kConnectResult && len >= 2) {
            AUDIO_LOG("RecieveVmfMsg", 0x3c2,
                      " ==== HMI ==== INFO: VAudioProxy::RecieveVmfMsg: AUDIO_CM_BUILD_SOURCE_CONNECT_RESULT %d", data[1]);
            // TODO(phase3): CarPlay / Android Auto / CarLife audio focus
            // replies (0x0004a0e0..0x0004a1a0).
            m_connect_result = 1;
        }
        // TODO(phase3): 0xc0 / 0xc2 connection state.
    } else if (grp == kGrpAudioAm) {
        const uint8_t sel = data[0];
        if (IsCallChannel(m_cur_channel) && sel != kAmPrimaryVolume && sel != kAmHmiMute) {
            return;
        }
        if (sel == kAmPrimaryVolume && DecodeAudioVolumeOrMute(data, len, &m_volume)) {
            // TODO(phase3): media pause / resume on volume 0 (0x0004a798).
            AUDIO_LOG("RecieveVmfMsg", 0x50e,
                      " ==== HMI ==== INFO: VAudioProxy::RecieveVmfMsg: AUDIO_AM_PRIMARY_VOLUME_STATUS %d %d %d",
                      m_volume.active, m_volume.primary_p2, m_volume.primary_p3);
        } else if (sel == kAmHmiMute && DecodeAudioVolumeOrMute(data, len, &m_volume)) {
            m_mute_p4 = len >= 5 ? data[4] : 0;
            AUDIO_LOG("RecieveVmfMsg", 0x52e,
                      " ==== HMI ==== INFO: VAudioProxy::RecieveVmfMsg: AUDIO_AM_HMI_MUTE_STATUS %d %d %d %d", data[1],
                      m_volume.mute_p2, m_volume.mute_p3, m_mute_p4);
            // TODO(phase3): media pause on mute, VDisplayProxy mute icon
            // (0x000502b0).
        } else {
            // TODO(phase3): max-on volume, APS, AVC, balance, fader, EQ, ...
            return;
        }
    } else {
        return;  // TODO(phase3): group 0x45
    }
    // TODO(phase3): stock forwards every handled message to
    // VControlTask (0x000308d8) and VDisplayTask (0x003af898).
}
