// File name is ours; stock name unknown.
#include "control_logic/subsystem/proxy_decoders.h"

namespace {

const uint8_t kVipAudioModeSelector = 0x81;  // MSG_VIPHMI_APHMI_AUDIO_MODE
const uint8_t kAudioPrimaryVolumeStatus = 0x00;
const uint8_t kAudioHmiMuteStatus = 0x04;

// 0x003f3ac4 kMainPageByAudioSource
// Logical main page for audio sources 3..16.
const int kMainPageByAudioSource[14] = {2, 2, 2, 2, 2, 2, 4, 5, 6, 7, 10, 1, 1, 2};
const int kFirstMappedAudioSource = 3;
const int kTunerMainPage = 1;
const int kMirrorLinkPage = 10;

// 0x003f3850 kScreenByPage
// IGDT screen id per logical page. Stock indexes it unchecked; entries past
// 0x14 are zero up to the next table.
const uint8_t kScreenByPage[0x15] = {
    0x00, 0xf9, 0x52, 0x0a, 0x63, 0x01, 0x1c, 0x1d, 0x02, 0x04, 0x00,
    0xee, 0x1a, 0x39, 0xdf, 0x25, 0xec, 0xe1, 0x5c, 0x26, 0x59,
};

} // namespace

bool DecodePowerDevStatus(const uint8_t* data, uint16_t len, PowerDevStatus* out) {
    if (data == 0 || out == 0 || len < 1) {
        return false;
    }
    out->acc_status = data[0] & 0x03;
    out->ignition_status = (data[0] & 0x0c) >> 2;
    return true;
}

bool DecodePowerModeChange(const uint8_t* data, uint16_t len, uint8_t* mode_out) {
    if (data == 0 || mode_out == 0 || len < 1) {
        return false;
    }
    *mode_out = data[0];
    return true;
}

bool DecodeVipAudioMode(const uint8_t* data, uint16_t len, uint8_t* audio_mode_out) {
    if (data == 0 || audio_mode_out == 0 || len < 2 || data[0] != kVipAudioModeSelector) {
        return false;
    }
    *audio_mode_out = data[1];
    return true;
}

bool DecodeAppConnectPss(const uint8_t* data, uint16_t len, AppConnectCoding* state) {
    // Both records need pData[5].
    if (data == 0 || state == 0 || len < 6) {
        return false;
    }
    if (data[2] == 0) {
        state->region_mode = data[3];
        state->sub_mode = data[5];
        return true;
    }
    if (data[2] == 1) {
        // DEVIATION: 0x003474a8 reads pData[5..7] and pData[13] whatever the
        // length; a short record here only updates the flags it carries.
        state->cp = data[5] != 0;
        if (len > 6) state->cl = data[6] != 0;
        if (len > 7) state->ml = data[7] != 0;
        if (len > 13) state->aa = data[13] != 0;
        return true;
    }
    return false;
}

bool DecodeBapEnvStatus(const uint8_t* data, uint16_t len, BapExtTemp* out) {
    if (data == 0 || out == 0 || len < 7) {
        return false;
    }
    // Stock stores the raw byte (+0x23c, read back by 0x000b6cc8); the
    // half-degree conversion is ours.
    out->temp_half_c = static_cast<int16_t>(static_cast<int>(data[1]) - 100);
    out->unit_f = data[2];
    out->invalid = data[3] == 1 || data[1] == 0;
    out->night_mode = data[5] != 0;
    return true;
}

bool DecodeAudioVolumeOrMute(const uint8_t* data, uint16_t len, AudioVolumeStatus* state) {
    if (data == 0 || state == 0 || len < 4) {
        return false;
    }
    if (data[0] == kAudioPrimaryVolumeStatus) {
        state->active = data[1];
        state->primary_p2 = data[2];
        state->primary_p3 = data[3];
        return true;
    }
    if (data[0] == kAudioHmiMuteStatus) {
        state->muted = data[1] == 1;
        state->mute_p2 = data[2];
        state->mute_p3 = data[3];
        return true;
    }
    return false;
}

int MainPageForAudioSource(int audio_src) {
    const int index = audio_src - kFirstMappedAudioSource;
    if (index < 0 || index >= static_cast<int>(sizeof(kMainPageByAudioSource) / sizeof(kMainPageByAudioSource[0]))) {
        return kTunerMainPage;
    }
    return kMainPageByAudioSource[index];
}

uint8_t ScreenForPage(int page_id) {
    if (page_id < 0 || page_id >= static_cast<int>(sizeof(kScreenByPage))) {
        return 0;
    }
    return kScreenByPage[page_id];
}

const uint8_t kPhase2PageScreens[3] = {0xf9, 0x04, 0x03};

bool ScreenHasPhase2Page(uint8_t scr_id) {
    for (unsigned i = 0; i < sizeof(kPhase2PageScreens); ++i) {
        if (kPhase2PageScreens[i] == scr_id) {
            return true;
        }
    }
    return false;
}

bool PageHasPhase2Screen(int page_id) {
    const uint8_t scr = ScreenForPage(page_id);
    return page_id != kMirrorLinkPage && scr != 0 && ScreenHasPhase2Page(scr);
}

Phase2PageAction ResolvePhase2Page(int page_id, int last_main_page, int* show_page) {
    if (show_page == 0) {
        return kPhase2PageIgnore;
    }
    if (PageHasPhase2Screen(page_id)) {
        *show_page = page_id;
        return kPhase2PageShow;
    }
    const bool main_page = page_id >= 1 && page_id < kMirrorLinkPage;
    if (main_page && last_main_page == 0) {
        *show_page = kTunerMainPage;
        return kPhase2PageFallBack;
    }
    return kPhase2PageIgnore;
}

PowerRunAction EvaluatePowerRunMode(PowerRunFlags* flags, uint8_t audio_mode) {
    if (flags == 0) {
        return kPowerRunIgnored;
    }
    if (flags->startup_state == 1) {
        return kPowerRunIgnored;  // pwr_agent is still in WS_IDLE / hmi not started
    }
    if (flags->hmi_run == 1) {
        // 0x0005d7e4: a repeated RUN only passes once OnSaveAudioModeToVIP
        // (0x00047810) moved running_init from 1 to 2.
        if (flags->running_init != 2) {
            return kPowerRunIgnored;
        }
        flags->running_init = 1;
    }
    flags->hmi_run = 1;
    if (audio_mode == 0) {
        return kPowerRunNoAudioYet;  // stock returns here (0x0005bab4, after the camera checks)
    }
    return kPowerRunShowMainPage;
}

// Part of 0x0005bab4: the wake-state notify (group 0x6e, sub 200). Name is
// ours. Stock clears hmi_run (+0x04) for every wake state, known or not.
bool ApplyPowerWakeState(PowerRunFlags* flags, uint8_t wake_state) {
    if (flags == 0) {
        return false;
    }
    bool running_cascade = false;
    if (wake_state == kWakeStateIdle) {
        flags->startup_state = 1;
        flags->running_init = 0;
    } else if (wake_state == kWakeStateRunning) {
        flags->running_init = 1;
        running_cascade = true;
    }
    flags->hmi_run = 0;
    return running_cascade;
}

const uint8_t kGetAudioModeFromVipMsg[5] = {0x8f, 0x27, 0x00, 0x01, 0x02};

// Part of 0x001c5a90 (each PSS block's read reply). Name is ours.
PssReadOutcome ClassifyPssReadStatus(uint8_t status) {
    if (status >= 10) {
        return kPssReadIgnored;
    }
    if ((1u << status) & 0x3ecu) {
        return kPssReadFailed;
    }
    return status == 0 ? kPssReadOk : kPssReadIgnored;
}

// Part of 0x001c5a90 (block 0x4b read failure). Name is ours. Stock stores the result in the setup object (0x001ffc10 +5 via
// 0x001fe084) and passes 1 and 0xff on unchanged, anything else as 0.
uint8_t SetupNewBlkFailedRegion(int region_mode, uint8_t platform_type) {
    if (region_mode != 2) {
        return 1;
    }
    return platform_type == 1 ? 0xff : 0;
}

// Part of 0x000509b0 (the language choice). Name is ours.
bool RegionToLanguage(uint8_t region, int region_mode, uint8_t country, int* lang) {
    if (lang == 0) {
        return false;
    }
    if (region <= 4) {
        *lang = region;
        return true;
    }
    if (region != 0xff) {
        return false;
    }
    if (region_mode != 2) {
        *lang = country == 9 ? 1 : 0;
    } else if (country == 0x0d) {
        *lang = 3;
    } else if (country == 0x15 || country == 0x06) {
        *lang = 2;
    } else if (country == 0x05 || country == 0x13) {
        *lang = 4;
    } else {
        *lang = 0;
    }
    return true;
}

// Ours (no stock counterpart; see kPssStatusNotReady).
bool ShouldRetrySetupRead(uint8_t status, int retries_done) {
    return status == kPssStatusNotReady && retries_done < kSetupReadMaxRetries;
}

// Ours (no stock counterpart; see kPssStatusNotReady).
void ResetSetupReadRetry(SetupReadRetryState* state) {
    if (state == 0) {
        return;
    }
    state->retries = 0;
    state->gave_up = false;
}

// Ours (no stock counterpart; see kPssStatusNotReady).
SetupReadRetryStep OnSetupReadReply(SetupReadRetryState* state, uint8_t status) {
    if (state == 0) {
        return kSetupReadNoRetry;
    }
    if (status != kPssStatusNotReady) {
        ResetSetupReadRetry(state);
        return kSetupReadNoRetry;
    }
    if (ShouldRetrySetupRead(status, state->retries)) {
        ++state->retries;
        return state->retries == 1 ? kSetupReadRetryFirst : kSetupReadRetry;
    }
    if (state->gave_up) {
        return kSetupReadNoRetry;
    }
    state->gave_up = true;
    return kSetupReadGiveUp;
}

const uint8_t kPssReadSystemSetupMsg[10] = {0x8c, 0x00, 0x00, 0x06, 0x72, 0x4b, 0x00, 0x00, 0x00, 0x14};
