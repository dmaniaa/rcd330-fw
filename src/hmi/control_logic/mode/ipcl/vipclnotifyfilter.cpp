// Stock source: control_logic/mode/ipcl/vipclnotifyfilter.cpp
//
// Phase 2 ports the key state machine, RADIO, APP and the tune keys. Phone,
// CarPlay / Android Auto / CarLife, camera and popup branches of the stock
// handlers are TODO(phase3) where they sit; MEDIA, VOICE, SETUP, PHONE and
// EJECT log once and pass the message on.
#include "control_logic/mode/ipcl/vipclnotifyfilter.h"

#include <string.h>

#include "control_logic/mode/ifilteractions.h"

namespace {

const char kFile[] = "control_logic/mode/ipcl/vipclnotifyfilter.cpp";

const int kKeyStatePress = 1;
const int kKeyStateRelease = 2;
// Re-posted key states (VHMIKey callbacks).
const int kKeyShortRelease = 4;
const int kKeyLongRelease = 6;
const int kKeyExtendRelease = 8;
const int kKeyUltralongRelease = 10;
const int kKeyUltralongPressEvent = 9;  // RADIO handles 9 separately

const int kKeyTimerMs = 1000;
const uint32_t kLongPressMs = 2000;
const int kLastKeyCode = 0x0b;

const uint8_t kScrTuner = 0xf9;
const uint8_t kScrRadioDebounce = 0xec;
const int kPageTuner = 1;
const int kPageAppConnectMenu = 8;
const int kPageAppNoConnection = 9;

const int kAudioModeTuner = 1;
const int kAudioModeTuner2 = 2;
const int kAudioModePhone = 9;

// VAppConnectProxy region modes that have an App-Connect page.
const int kRegionMode1 = 1;
const int kRegionMode2 = 2;

// App-Connect / projection screens: APP from one of them opens the menu
// (page 8) instead of the no-connection page (0x00041c40).
const uint8_t kAppScreens[6] = {0x01, 0x55, 0x54, 0x56, 0x1d, 0x1c};

// Popups that keep RADIO from re-showing the tuner while TA/scan is up
// (tuner state 0x0c/0x0d branch of 0x00042934).
const uint8_t kRadioBlockScreens[8] = {0xee, 0x02, 0x04, 0x39, 0x54, 0x55, 0x56, 0x57};

IFilterActions* ActionsOf(void* ctx) {
    return static_cast<IFilterActions*>(ctx);
}

// Stock callbacks build {2, 4, {4, 1, key, out_state}} and hand it to
// VControlTask 0x00032a60, which only emits SigKeyEventNotify(key, state).
// Every callback but the press one stops the key timer first.

// 0x000433e0 hard_key_short_press
// Despite its stock name this is the press callback (old state 1).
void HardKeyPress(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    FilterLog(kFile, "hard_key_short_press", 0x87c, " ==== HMI ==== INFO: hard_key_short_press key_id=%d", key);
    act->NotifyDisplayKey(static_cast<uint8_t>(key), old_state == 1 ? 3 : 0);
}

// 0x000438d4 hard_key_short_release
// Name is ours (stock logs nothing here).
void HardKeyShortRelease(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    act->StopKeyTimer();
    const uint8_t state = old_state == 3 ? 4 : 0;
    act->NotifyDisplayKey(static_cast<uint8_t>(key), state);
    act->NotifyBapKey(static_cast<uint8_t>(key), state);
}

// 0x000437d0 hard_key_long_press
void HardKeyLongPress(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    FilterLog(kFile, "hard_key_long_press", 0x8bb, " ==== HMI ==== INFO: hard_key_long_press key_id key_id=%d ", key);
    act->StopKeyTimer();
    const uint8_t state = old_state == 4 ? 5 : 0;
    act->NotifyDisplayKey(static_cast<uint8_t>(key), state);
    act->NotifyBapKey(static_cast<uint8_t>(key), state);
}

// 0x000436cc hard_key_long_release
void HardKeyLongRelease(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    FilterLog(kFile, "hard_key_long_release", 0x8de, " ==== HMI ==== INFO: hard_key_long_release key_id key_id=%d", key);
    act->StopKeyTimer();
    const uint8_t state = old_state == 7 ? 6 : 0;
    act->NotifyDisplayKey(static_cast<uint8_t>(key), state);
    act->NotifyBapKey(static_cast<uint8_t>(key), state);
}

// 0x000432f0 hard_key_extend_long_press
void HardKeyExtendLongPress(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    act->StopKeyTimer();
    act->NotifyDisplayKey(static_cast<uint8_t>(key), old_state == 6 ? 7 : 0);
}

// 0x000435c8 hard_key_extend_long_release
// STOCK-QUIRK: BAP always gets state 6 here, not the 8 sent to the display.
void HardKeyExtendLongRelease(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    act->StopKeyTimer();
    act->NotifyDisplayKey(static_cast<uint8_t>(key), old_state == 9 ? 8 : 0);
    act->NotifyBapKey(static_cast<uint8_t>(key), 6);
}

// 0x00043200 hard_key_ultralong_press
void HardKeyUltralongPress(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    act->StopKeyTimer();
    act->NotifyDisplayKey(static_cast<uint8_t>(key), old_state == 10 ? 9 : 0);
}

// 0x000434c0 hard_key_ultralong_release
// STOCK-QUIRK: BAP always gets state 6 here, not the 10 sent to the display.
void HardKeyUltralongRelease(void* ctx, int key, int old_state) {
    IFilterActions* act = ActionsOf(ctx);
    act->StopKeyTimer();
    act->NotifyDisplayKey(static_cast<uint8_t>(key), old_state == 12 ? 10 : 0);
    act->NotifyBapKey(static_cast<uint8_t>(key), 6);
}

// The handlers' common return: para[0], para[1], type and valid may be
// rewritten; the rest of the message is kept.
FILTER_MSG Rewrite(const FILTER_MSG& msg, uint8_t valid, uint32_t type, uint8_t p0, uint8_t p1) {
    FILTER_MSG out = msg;
    out.valid = valid;
    out.type = type;
    out.para[0] = p0;
    out.para[1] = p1;
    return out;
}

}  // namespace

// 0x0004398c VIPCLNotifyFilter::VIPCLNotifyFilter
VIPCLNotifyFilter::VIPCLNotifyFilter(IFilterActions* actions)
    : VBaseFilter(actions), m_radio_last_state(0), m_logged_unknown_key(false),
      m_logged_phase3_radio(false), m_logged_phase3_app(false) {
    memset(m_logged_key, 0, sizeof(m_logged_key));
    const int keys[10] = {2, 1, 5, 3, 4, 0x0b, 6, 7, 8, 10};
    for (int i = 0; i < 10; ++i) {
        m_keys.AddKey(keys[i]);
    }
    m_keys.SetLongPressTime(kLongPressMs);
    VHMIKeyCallbacks cbs;
    cbs.press = HardKeyPress;
    cbs.short_release = HardKeyShortRelease;
    cbs.long_press = HardKeyLongPress;
    cbs.long_release = HardKeyLongRelease;
    cbs.extend_long_press = HardKeyExtendLongPress;
    cbs.extend_long_release = HardKeyExtendLongRelease;
    cbs.ultralong_press = HardKeyUltralongPress;
    cbs.ultralong_release = HardKeyUltralongRelease;
    // A NULL action sink would crash the callbacks: register none then.
    if (actions != 0) {
        m_keys.SetCallbacks(cbs, actions);
    }
}

// 0x00043194 VIPCLNotifyFilter::~VIPCLNotifyFilter
VIPCLNotifyFilter::~VIPCLNotifyFilter() {}

void VIPCLNotifyFilter::KeyTimeout(uint32_t elapsed_ms) {
    m_keys.Tick(elapsed_ms);
}

// 0x00042fdc VIPCLNotifyFilter::HandleLPCLHardKey
// Driver press / release only move the key state (and are consumed); any
// other state is a re-posted key event for the per-key handlers.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKey(const FILTER_MSG& msg) {
    const int key = msg.para[2];
    const int state = msg.para[3];
    FilterLog(kFile, "HandleLPCLHardKey", 0xae, " ==== HMI ==== INFO: VIPCLNotifyFilter::HandleLPCLHardKey key=%d state=%d",
              key, state);
    if (key == 0 || key > kLastKeyCode) {
        if (!m_logged_unknown_key) {
            m_logged_unknown_key = true;
            FilterLog(kFile, "HandleLPCLHardKey", 0xae, " ==== HMI ==== WARN: unknown hard key %d dropped (logged once)", key);
        }
        FILTER_MSG dropped = msg;
        dropped.valid = kFilterInvalid;
        return dropped;
    }
    if (state == kKeyStatePress) {
        m_keys.Press(key);
        if (m_actions != 0) {
            m_actions->StartKeyTimer(kKeyTimerMs);
        }
        FILTER_MSG out = msg;
        out.valid = kFilterInvalid;
        return out;
    }
    if (state == kKeyStateRelease) {
        if (m_actions != 0) {
            m_actions->StopKeyTimer();
        }
        m_keys.Release(key);
        if (m_actions != 0 && m_actions->IsMultiHardkeyPermit()) {
            m_keys.ResetAll();
        }
        FILTER_MSG out = msg;
        out.valid = kFilterInvalid;
        return out;
    }
    return VBaseFilter::HandleLPCLHardKey(msg);
}

FILTER_MSG VIPCLNotifyFilter::Phase3Key(const FILTER_MSG& msg, const char* name) {
    const int key = msg.para[2];
    if (key >= 0 && key < kKeyCodeCount && !m_logged_key[key]) {
        m_logged_key[key] = true;
        FilterLog(kFile, name, 0, " ==== HMI ==== WARN: %s (key %d) is not implemented in phase 2; ignored (logged once)",
                  name, key);
    }
    return msg;
}

// 0x00043c1c VIPCLNotifyFilter::HandleLPCLHardKeyMedia
// TODO(phase3): media source switching.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyMedia(const FILTER_MSG& msg) {
    return Phase3Key(msg, "HandleLPCLHardKeyMedia");
}

// 0x00042570 VIPCLNotifyFilter::HandleLPCLHardKeyPhone
// TODO(phase3): phone page / call handling.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyPhone(const FILTER_MSG& msg) {
    return Phase3Key(msg, "HandleLPCLHardKeyPhone");
}

// 0x00041470 VIPCLNotifyFilter::HandleLPCLHardKeyVoice
// Name is ours (stock logs nothing). TODO(phase3): voice / mute key.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyVoice(const FILTER_MSG& msg) {
    return Phase3Key(msg, "HandleLPCLHardKeyVoice");
}

// 0x00042088 VIPCLNotifyFilter::HandleLPCLHardKeySet
// TODO(phase3): setup pages.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeySet(const FILTER_MSG& msg) {
    return Phase3Key(msg, "HandleLPCLHardKeySet");
}

// 0x00041374 VIPCLNotifyFilter::HandleLPCLHardKeyEject
// Name is ours (stock logs nothing). TODO(phase3): CD eject.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyEject(const FILTER_MSG& msg) {
    return Phase3Key(msg, "HandleLPCLHardKeyEject");
}

// 0x00041b74 VIPCLNotifyFilter::HandleLPCLHardKeyTune
// TODO(phase3): with popup 0x4f shown stock calls 0x0010f8c0.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyTune(const FILTER_MSG& msg) {
    FilterLog(kFile, "HandleLPCLHardKeyTune", 0x69f, " ==== HMI ==== INFO: VIPCLNotifyFilter HandleLPCLHardKeyTune:%d %d",
              msg.para[2], msg.para[3]);
    return msg;
}

// 0x00041ab8 VIPCLNotifyFilter::HandleLPCLHardKeyTuneUp
// TODO(phase3): with popup 0x4f shown stock calls 0x0010f8c0.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyTuneUp(const FILTER_MSG& msg) {
    FilterLog(kFile, "HandleLPCLHardKeyTuneUp", 0x6ee, " ==== HMI ==== INFO: VIPCLNotifyFilter HandleLPCLHardKeyTuneUp:%d",
              msg.para[3]);
    return msg;
}

// 0x00041990 VIPCLNotifyFilter::HandleLPCLHardKeyTuneDown
// Short release in tuner mode asks the AMFM command filter for {0x0c, 7}.
// TODO(phase3): popup 0x4f (0x0010f8c0) suppresses it in stock.
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyTuneDown(const FILTER_MSG& msg) {
    const int mode = m_actions->GetAudioMode();
    FilterLog(kFile, "HandleLPCLHardKeyTuneDown", 0x6c6, " ==== HMI ==== INFO: VIPCLNotifyFilter HandleLPCLHardKeyTuneDown:%d",
              msg.para[3]);
    if (msg.para[3] == kKeyShortRelease && (mode == kAudioModeTuner || mode == kAudioModeTuner2)) {
        return Rewrite(msg, kFilterCmd, 2, 0x0c, 7);
    }
    return msg;
}

// 0x00042934 VIPCLNotifyFilter::HandleLPCLHardKeyRadio
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyRadio(const FILTER_MSG& msg) {
    IFilterActions* act = m_actions;
    const int s = msg.para[3];

    // While screen 0xec is up, a release only counts after its own press
    // event (3/5/7/9 -> 4/6/8/10); stock keeps the last state in a global.
    if (act->IsScreenInStack(kScrRadioDebounce) || act->GetCurrentScreen() == kScrRadioDebounce) {
        const int last = m_radio_last_state;
        bool accept;
        if (s == kKeyShortRelease) {
            accept = last == 3;
        } else if (s == kKeyLongRelease) {
            accept = last == 5;
        } else if (s == kKeyExtendRelease) {
            accept = last == 7;
        } else {
            accept = s != kKeyUltralongRelease || last == 9;
        }
        m_radio_last_state = s;
        if (!accept) {
            return msg;
        }
    }

    // TODO(phase3): stock restarts the source timer (0x00030728, 1000 ms).
    const int mode = act->GetAudioMode();
    const int band = act->GetTunerBand();
    const int tuner_state = act->GetTunerState();
    FilterLog(kFile, "HandleLPCLHardKeyRadio", 0x2d6, " ==== HMI ==== INFO: VIPCLNotifyFilter::HandleLPCLHardKeyRadio audio_mode=%d",
              mode);
    // TODO(phase3): with popup 0x4f shown, 0x0010f8c0 may swallow the key.

    bool act_release = false;
    if (!act->IsHMIOn()) {
        if (act->IsTempProtect()) {
            act_release = s == kKeyShortRelease || s == kKeyLongRelease;
        } else if (mode == kAudioModeTuner || mode == kAudioModeTuner2) {
            if (s == kKeyUltralongPressEvent) {
                return msg;
            }
            act_release = s == kKeyShortRelease || s == kKeyLongRelease;
            if (act_release || s == kKeyExtendRelease || s == kKeyUltralongRelease) {
                if (act->GetCurrentScreen() == kScrTuner) {
                    act->UpdateBacklight();
                    return msg;
                }
                // TODO(phase3): stock sets VPowerProxy +0x0b (key wake) here.
            }
        } else {
            act_release = s == kKeyShortRelease || s == kKeyLongRelease;
            // TODO(phase3): for releases stock sets VPowerProxy +0x0b.
        }
    } else {
        act_release = s == kKeyShortRelease || s == kKeyLongRelease;
    }

    if (mode == kAudioModePhone) {
        // TODO(phase3): phone audio: call popup 0x1c unless page 0x7f is up.
        if (!m_logged_phase3_radio) {
            m_logged_phase3_radio = true;
            FilterLog(kFile, "HandleLPCLHardKeyRadio", 0x2d6, " ==== HMI ==== WARN: RADIO during phone audio is phase 3 (logged once)");
        }
        return msg;
    }

    // TODO(phase3): stock first calls VMediaProxy 0x000d8814(0), checks the
    // CarPlay state (0x0032d4f0, popup 0x1c) and BT (0x000c54ec).
    if (act_release || s == kKeyExtendRelease || s == kKeyUltralongRelease) {
        if (tuner_state == 6 || tuner_state == 8) {
            if (act->GetCurrentScreen() != kScrTuner) {
                act->InsertPage(kPageTuner);
            }
            // TODO(phase3): VAMFMProxy 0x000adbd0.
            return msg;
        }
        if (tuner_state != 0x0c && tuner_state != 0x0d) {
            if (mode == kAudioModeTuner || mode == kAudioModeTuner2) {
                if (act->GetCurrentScreen() != kScrTuner) {
                    act->InsertPage(kPageTuner);
                    act->QueryTunerInfo();
                    // TODO(phase3): camera close (0x0004bdb4, 0x0004c8b0).
                }
                return msg;
            }
            // Another source plays: ask the audio command filter to switch
            // to the tuner; the page follows the audio mode change.
            // TODO(phase3): camera close (0x0004bdb4, 0x0004c8b0).
            return Rewrite(msg, kFilterCmd, 2, 2, band < 3 ? 1 : 2);
        }
        // TA / scan running (tuner state 0x0c / 0x0d).
        if (act->GetBaseScreen() == kScrTuner) {
            bool blocked = false;
            for (size_t i = 0; i < sizeof(kRadioBlockScreens); ++i) {
                if (act->IsScreenShown(kRadioBlockScreens[i])) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                return msg;
            }
        }
        act->InsertPage(kPageTuner);
        // TODO(phase3): camera close (0x0004bdb4, 0x0004c8b0).
        if (mode != kAudioModeTuner && mode != kAudioModeTuner2) {
            return Rewrite(msg, kFilterCmd, 2, 2, band < 3 ? 1 : 2);
        }
        return msg;
    }
    if (s == kKeyUltralongPressEvent && (mode == kAudioModeTuner || mode == kAudioModeTuner2) &&
        act->GetCurDisplayPage() == kPageTuner) {
        // TODO(phase3): VAudioProxy 0x000490cc and camera close first.
        return Rewrite(msg, kFilterCmd, 2, 0x0c, 0x10);
    }
    return msg;
}

// 0x00041c40 VIPCLNotifyFilter::HandleLPCLHardKeyApp
FILTER_MSG VIPCLNotifyFilter::HandleLPCLHardKeyApp(const FILTER_MSG& msg) {
    IFilterActions* act = m_actions;
    const int s = msg.para[3];
    // TODO(phase3): with popup 0x4f shown, 0x0010f8c0 may swallow the key;
    // stock also calls VMediaProxy 0x000d8814(0) first.

    bool act_release;
    if (!act->IsHMIOn() && !act->IsTempProtect()) {
        if (s == kKeyShortRelease || s == kKeyLongRelease) {
            act_release = true;
        } else if (s == kKeyExtendRelease || s == kKeyUltralongRelease) {
            act_release = false;
        } else {
            return msg;
        }
        // TODO(phase3): stock sets VPowerProxy +0x0b (key wake) here.
    } else {
        act_release = s == kKeyShortRelease || s == kKeyLongRelease;
    }
    if (!act_release) {
        return msg;
    }
    const int region = act->GetRegionMode();
    if (region != kRegionMode1 && region != kRegionMode2) {
        return msg;
    }
    // TODO(phase3): BT 0x000c54ec / 0x000c21e8 and audio mode 9 first.

    const int cur = act->GetCurrentScreen();
    for (size_t i = 0; i < sizeof(kAppScreens); ++i) {
        if (cur == kAppScreens[i] || act->IsScreenInStack(kAppScreens[i])) {
            act->InsertPage(kPageAppConnectMenu);
            return msg;
        }
    }
    if (act->GetAppDeviceType() > 1) {
        // TODO(phase3): connected CarPlay / Android Auto / CarLife /
        // MirrorLink device: projection launch (0x0034707c, 0x003497e8, ...).
        if (!m_logged_phase3_app) {
            m_logged_phase3_app = true;
            FilterLog(kFile, "HandleLPCLHardKeyApp", 0, " ==== HMI ==== WARN: APP with a connected device is phase 3 (logged once)");
        }
        return msg;
    }
    act->InsertPage(kPageAppNoConnection);
    return msg;
}
