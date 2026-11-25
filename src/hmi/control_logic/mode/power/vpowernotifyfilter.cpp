// Stock source: control_logic/mode/power/vpowernotifyfilter.cpp
#include "control_logic/mode/power/vpowernotifyfilter.h"

#include "control_logic/mode/ifilteractions.h"

namespace {

const char kFile[] = "control_logic/mode/power/vpowernotifyfilter.cpp";

const int kMirrorLinkConnected = 2;

}  // namespace

// 0x000465fc VPowerNotifyFilter::VPowerNotifyFilter
VPowerNotifyFilter::VPowerNotifyFilter(IFilterActions* actions) : VBaseFilter(actions), m_logged_aux(false) {}

// 0x00045fa4 VPowerNotifyFilter::HandlePowerVOLStatus
// Turns the notification into the volume command {0x0e, 1, value}.
FILTER_MSG VPowerNotifyFilter::HandlePowerVOLStatus(const FILTER_MSG& msg) {
    FilterLog(kFile, "HandlePowerVOLStatus", 0xa0, " ==== HMI ==== INFO: VPowerNotifyFilter::HandlePowerVOLStatus");
    FILTER_MSG out = msg;
    out.valid = kFilterCmd;
    out.type = 2;
    out.para[0] = 0x0e;
    out.para[1] = 1;
    return out;
}

// 0x00046250 VPowerNotifyFilter::HandlePowerAuxStatus
// TODO(phase3): aux-in plug / unplug: source switch requests depending on
// the audio mode, MirrorLink (0x001afa10) and App-Connect region. Passed on
// unchanged until then.
FILTER_MSG VPowerNotifyFilter::HandlePowerAuxStatus(const FILTER_MSG& msg) {
    FilterLog(kFile, "HandlePowerAuxStatus", 0xb0, " ==== HMI ==== INFO: VPowerNotifyFilter::HandlePowerAuxStatus");
    if (!m_logged_aux) {
        m_logged_aux = true;
        FilterLog(kFile, "HandlePowerAuxStatus", 0xb0, " ==== HMI ==== WARN: aux status handling is phase 3 (logged once)");
    }
    return msg;
}

// 0x00045ec4 VPowerNotifyFilter::HandlePowerEjectStatus
// Eject state 0 / 1 becomes the media command {0x0b, 9 / 10}.
FILTER_MSG VPowerNotifyFilter::HandlePowerEjectStatus(const FILTER_MSG& msg) {
    FilterLog(kFile, "HandlePowerEjectStatus", 0x7c, " ==== HMI ==== INFO: VPowerNotifyFilter::HandlePowerEjectStatus");
    FILTER_MSG out = msg;
    if (msg.para[2] == 0 || msg.para[2] == 1) {
        out.valid = kFilterCmd;
        out.type = 2;
        out.para[0] = 0x0b;
        out.para[1] = msg.para[2] == 0 ? 9 : 10;
    }
    return out;
}

// 0x00046054 VPowerNotifyFilter::HandleAudioModeStatus
// para[2]: high nibble = audio source, low nibble = explicit page (0 = the
// source's main page). Sources 2..9 map to audio mode n, 4 (aux / USB) only
// when that device is present, 0x0d (MirrorLink) only while connected;
// everything else is the tuner (mode 1).
// Stock calls VBTProxy 0x000c2b90 and ignores the result.
FILTER_MSG VPowerNotifyFilter::HandleAudioModeStatus(const FILTER_MSG& msg) {
    const int status = msg.para[2];
    FilterLog(kFile, "HandleAudioModeStatus", 0x139, " ==== HMI ==== INFO: VPowerNotifyFilter::HandleAudioModeStatus %d",
              status);
    const int media_flags = m_actions->GetMediaDeviceFlags();
    const int aux_mode = m_actions->GetAuxMode();
    int mode = 1;
    int index = 0;  // stock uVar1: tuner info is queried for index < 2
    switch (status >> 4) {
    case 2: case 3: case 5: case 6: case 7: case 8: case 9:
        mode = status >> 4;
        index = mode - 1;
        break;
    case 4:
        if (((media_flags | (aux_mode << 4)) & 0x10) != 0) {
            mode = 4;
            index = 3;
        }
        break;
    case 0x0d:
        if (m_actions->GetMirrorLinkState() == kMirrorLinkConnected) {
            mode = 0x0d;
            index = 0x0c;
        }
        break;
    default:
        break;
    }
    m_actions->SetAudioMode(mode);
    if ((status & 0x0f) == 0) {
        m_actions->SetMainPage(mode, 1);
    } else {
        m_actions->InsertPage(status & 0x0f);
    }
    if (index < 2) {
        m_actions->QueryTunerInfo();
    }
    return msg;
}
