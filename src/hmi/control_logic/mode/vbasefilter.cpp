// File name is ours; stock name unknown.
#include "control_logic/mode/vbasefilter.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "control_logic/mode/ifilteractions.h"

#ifndef HMI_HOST_TEST
#include "service/osal/osal_api.h"
#endif

void FILTER_MSG::Reset() {
    memset(this, 0, sizeof(*this));
}

void FilterLog(const char* file, const char* func, int line, const char* fmt, ...) {
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
#ifdef HMI_HOST_TEST
    fprintf(stderr, "[%s:%d %s] %s\n", file, line, func, text);
#else
    // The QEMU log_simple hook (tools/arm_hal_shim) only echoes formats that
    // contain the " ==== HMI ==== " marker, so keep it in the format instead
    // of a bare "%s" (the device log text is the same either way).
    static const char kMarker[] = " ==== HMI ==== ";
    if (strncmp(text, kMarker, sizeof(kMarker) - 1) == 0) {
        log_simple("INFO", " ==== HMI ==== %s", text + sizeof(kMarker) - 1);
    } else {
        log_simple("INFO", "%s", text);
    }
    printlog(3, "INFO", file, func, line, "%s", text);
#endif
}

namespace {

// Stock pass-through: the message comes back unchanged.
FILTER_MSG Pass(const FILTER_MSG& msg) {
    return msg;
}

}  // namespace

// 0x0003af60 VBaseFilter::VBaseFilter
VBaseFilter::VBaseFilter(IFilterActions* actions) : m_actions(actions) {}

// 0x0003af18 VBaseFilter::~VBaseFilter
VBaseFilter::~VBaseFilter() {}

// 0x00039780 VBaseFilter::AddNextFilter
bool VBaseFilter::AddNextFilter(VBaseFilter* next) {
    if (next == 0) {
        return false;
    }
    for (size_t i = 0; i < m_next.size(); ++i) {
        if (m_next[i] == next) {
            return false;
        }
    }
    m_next.push_back(next);
    return true;
}

// 0x0003962c VBaseFilter::DispatchMsg
// Name is ours. Stock stops when the result's valid byte or type word is 0.
void VBaseFilter::DispatchMsg(const FILTER_MSG& msg) {
    const FILTER_MSG out = HandleMsg(msg);
    if (out.valid == kFilterInvalid || out.type == 0) {
        return;
    }
    for (size_t i = 0; i < m_next.size(); ++i) {
        if (m_next[i] != 0) {
            m_next[i]->DispatchMsg(out);
        }
    }
}

// 0x00039998 VBaseFilter::HandleMsg
FILTER_MSG VBaseFilter::HandleMsg(const FILTER_MSG& msg) {
    if (msg.type == 0) {
        FILTER_MSG out = msg;
        out.valid = kFilterInvalid;
        return out;
    }
    if (msg.valid == kFilterCmd) {
        return HandleCmdMsg(msg);
    }
    if (msg.valid == kFilterNotify) {
        return HandleNotifyMsg(msg);
    }
    return Pass(msg);
}

// 0x0003bbb0 VBaseFilter::HandleCmdMsg
FILTER_MSG VBaseFilter::HandleCmdMsg(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x00039a84 VBaseFilter::HandleNotifyMsg
// TODO(phase3): para[0] 3 and 5..0x0d (audio, AMFM, media, BT, camera, ...)
// have their own dispatchers (vtable slots 5..15); they pass through here.
FILTER_MSG VBaseFilter::HandleNotifyMsg(const FILTER_MSG& msg) {
    switch (msg.para[0]) {
    case kFilterSubPower:
        return HandlePowerNotify(msg);
    case kFilterSubIpcl:
        return HandleIPCLNotify(msg);
    default:
        return Pass(msg);
    }
}

// 0x00039d80 VBaseFilter::HandlePowerNotify
// TODO(phase3): para[1] 1 goes to the 0x00039ef4 sub-dispatcher (para[2]).
FILTER_MSG VBaseFilter::HandlePowerNotify(const FILTER_MSG& msg) {
    switch (msg.para[1]) {
    case 2:
        return HandlePowerVOLStatus(msg);
    case 3:
        return HandlePowerAuxStatus(msg);
    case 4:
        return HandlePowerEjectStatus(msg);
    case 5:
        return HandleAudioModeStatus(msg);
    default:
        return Pass(msg);
    }
}

// 0x0003af80 VBaseFilter::HandlePowerVOLStatus
FILTER_MSG VBaseFilter::HandlePowerVOLStatus(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003afb0 VBaseFilter::HandlePowerAuxStatus
FILTER_MSG VBaseFilter::HandlePowerAuxStatus(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003afe0 VBaseFilter::HandlePowerEjectStatus
FILTER_MSG VBaseFilter::HandlePowerEjectStatus(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003b010 VBaseFilter::HandleAudioModeStatus
FILTER_MSG VBaseFilter::HandleAudioModeStatus(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003a1a4 VBaseFilter::HandleIPCLNotify
// TODO(phase3): para[1] 2 and 3 (vtable slots 28/29) pass through.
FILTER_MSG VBaseFilter::HandleIPCLNotify(const FILTER_MSG& msg) {
    if (msg.para[1] == 1) {
        return HandleLPCLHardKey(msg);
    }
    return Pass(msg);
}

// 0x0003abd8 VBaseFilter::HandleLPCLHardKey
// Hard keys are dropped until the first igdt_show. Keys 1..6 and 10 reach
// their handler only while VKeyDriver still reports that key as the active
// one; 7..9 always do; 0x0b has no handler.
FILTER_MSG VBaseFilter::HandleLPCLHardKey(const FILTER_MSG& msg) {
    if (m_actions == 0 || !m_actions->IsIgdtShown()) {
        FILTER_MSG dropped;
        dropped.Reset();
        return dropped;
    }
    const int key = msg.para[2];
    const bool active = m_actions->GetActiveKeyCode() == key;
    switch (key) {
    case 1:
        return active ? HandleLPCLHardKeyRadio(msg) : Pass(msg);
    case 2:
        return active ? HandleLPCLHardKeyMedia(msg) : Pass(msg);
    case 3:
        return active ? HandleLPCLHardKeyVoice(msg) : Pass(msg);
    case 4:
        return active ? HandleLPCLHardKeySet(msg) : Pass(msg);
    case 5:
        return active ? HandleLPCLHardKeyPhone(msg) : Pass(msg);
    case 6:
        return active ? HandleLPCLHardKeyTune(msg) : Pass(msg);
    case 7:
        return HandleLPCLHardKeyTuneUp(msg);
    case 8:
        return HandleLPCLHardKeyTuneDown(msg);
    case 9:
        return HandleLPCLHardKeyEject(msg);
    case 10:
        return active ? HandleLPCLHardKeyApp(msg) : Pass(msg);
    default:
        return Pass(msg);
    }
}

// 0x0003b7f0 VBaseFilter::HandleLPCLHardKeyMedia
FILTER_MSG VBaseFilter::HandleLPCLHardKeyMedia(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003b820 VBaseFilter::HandleLPCLHardKeyRadio
FILTER_MSG VBaseFilter::HandleLPCLHardKeyRadio(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003b850 VBaseFilter::HandleLPCLHardKeyPhone
FILTER_MSG VBaseFilter::HandleLPCLHardKeyPhone(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003b8b0 VBaseFilter::HandleLPCLHardKeyVoice
FILTER_MSG VBaseFilter::HandleLPCLHardKeyVoice(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003b9a0 VBaseFilter::HandleLPCLHardKeySet
FILTER_MSG VBaseFilter::HandleLPCLHardKeySet(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003ba60 VBaseFilter::HandleLPCLHardKeyTune
FILTER_MSG VBaseFilter::HandleLPCLHardKeyTune(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003ba90 VBaseFilter::HandleLPCLHardKeyTuneUp
FILTER_MSG VBaseFilter::HandleLPCLHardKeyTuneUp(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003bac0 VBaseFilter::HandleLPCLHardKeyTuneDown
FILTER_MSG VBaseFilter::HandleLPCLHardKeyTuneDown(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003bb50 VBaseFilter::HandleLPCLHardKeyApp
FILTER_MSG VBaseFilter::HandleLPCLHardKeyApp(const FILTER_MSG& msg) {
    return Pass(msg);
}

// 0x0003bb80 VBaseFilter::HandleLPCLHardKeyEject
FILTER_MSG VBaseFilter::HandleLPCLHardKeyEject(const FILTER_MSG& msg) {
    return Pass(msg);
}
