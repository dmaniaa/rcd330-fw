// File name is ours; stock name unknown.
//
// Portable (no Qt, no VMF) decoders for the subsystem proxy payloads, lifted
// out of the stock RecieveVmfMsg bodies so they can be host-tested. Every
// decoder rejects a NULL buffer, a NULL output and a payload too short for the
// bytes it reads; stock reads those bytes unchecked.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PROXY_DECODERS_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PROXY_DECODERS_H

#include <stdint.h>

// PA_OUT_DEV_STS (0x0C:0x04), VPowerProxy::RecieveVmfMsg 0x0005bab4.
struct PowerDevStatus {
    uint8_t acc_status;       // pData[0] & 0x03 (m_ucACCStatus, +0x65)
    uint8_t ignition_status;  // (pData[0] & 0x0c) >> 2 (m_ucIgnitionStatus, +0x66)
};
bool DecodePowerDevStatus(const uint8_t* data, uint16_t len, PowerDevStatus* out);

// PA_OUT_MODE_CHG (0x0C:0x01): pData[0] is the power mode (3 = RUN, 6 = TEMP_ON).
bool DecodePowerModeChange(const uint8_t* data, uint16_t len, uint8_t* mode_out);

// VIP notification 0x74:0x01 with pData[0] == 0x81
// (MSG_VIPHMI_APHMI_AUDIO_MODE): pData[1] is the audio mode.
bool DecodeVipAudioMode(const uint8_t* data, uint16_t len, uint8_t* audio_mode_out);

// App-Connect coding read back from PSS (0x72:0x04 / 0x8D:0x04),
// VAppConnectProxy::RecieveVmfMsg 0x0034ac5c and 0x003474a8.
struct AppConnectCoding {
    uint32_t region_mode;  // +0x24, pData[3] when pData[2] == 0
    uint32_t sub_mode;     // +0x28, pData[5] when pData[2] == 0
    bool cp;               // +0x2e CarPlay,     pData[5]  when pData[2] == 1
    bool aa;               // +0x34 AndroidAuto, pData[13] when pData[2] == 1
    bool ml;               // +0x30 MirrorLink,  pData[7]  when pData[2] == 1
    bool cl;               // +0x2f CarLife,     pData[6]  when pData[2] == 1
};
// Updates only the fields the record carries; false for an unknown pData[2].
bool DecodeAppConnectPss(const uint8_t* data, uint16_t len, AppConnectCoding* state);

// BAP environment status (0xA2:0x05), VBAPProxy::RecieveVmfMsg 0x000b9970.
struct BapExtTemp {
    int16_t temp_half_c;  // pData[1] - 100: raw is (C + 50) * 2
    uint8_t unit_f;       // pData[2]
    bool invalid;         // pData[3] == 1 or pData[1] == 0
    bool night_mode;      // pData[5] != 0
};
bool DecodeBapEnvStatus(const uint8_t* data, uint16_t len, BapExtTemp* out);

// Audio manager status (group 0x3F), VAudioProxy::RecieveVmfMsg 0x00049f24.
// pData[0] == 0x00 is AUDIO_AM_PRIMARY_VOLUME_STATUS, 0x04 is
// AUDIO_AM_HMI_MUTE_STATUS; stock does not check the sub id.
struct AudioVolumeStatus {
    uint8_t active;      // volume: pData[1] (+0x16)
    uint8_t primary_p2;  // volume: pData[2] (+0x3a)
    uint8_t primary_p3;  // volume: pData[3] (+0x39)
    uint8_t mute_p2;     // mute:   pData[2] (+0x33)
    uint8_t mute_p3;     // mute:   pData[3] (+0x34)
    bool muted;          // mute:   pData[1] (+0x32)
};
// Updates only the volume or the mute fields; false for any other pData[0].
bool DecodeAudioVolumeOrMute(const uint8_t* data, uint16_t len, AudioVolumeStatus* state);

// VDisplayProxy::SetMainPage (0x00054fcc) audio source -> logical main page
// (table 0x003f3ac4 for sources 3..16, Tuner page 1 otherwise).
int MainPageForAudioSource(int audio_src);
// VDisplayProxy::InsertPage (0x00054314) logical page -> IGDT screen id
// (table 0x003f3850); 0 for a page past the table.
uint8_t ScreenForPage(int page_id);
// The IGDT screens VPageManager::ShowPage builds a page for in phase 2
// (0xf9 Tuner, 0x04 App-Connect idle, 0x03 App-Connect info). ShowPage
// gates its switch on this list; every other screen is phase 3.
extern const uint8_t kPhase2PageScreens[3];
bool ScreenHasPhase2Page(uint8_t scr_id);
// True when InsertPage can show the page in phase 2: its table screen is in
// kPhase2PageScreens. False for MirrorLink (10), which stock handles in its
// own branch and never maps through the table, for pages whose screen is
// phase 3 (e.g. 2 -> 0x52 media, 8 -> 0x02 App-Connect menu) and for pages
// without a screen (table value 0 or out of range).
bool PageHasPhase2Screen(int page_id);
// What VDisplayProxy::InsertPage does with a page request in phase 2. Name
// is ours (no stock counterpart: stock has every page).
enum Phase2PageAction {
    kPhase2PageShow = 0,      // show *show_page (= page_id)
    kPhase2PageFallBack = 1,  // show *show_page (= Tuner page 1) instead
    kPhase2PageIgnore = 2     // leave the page stack and the page state alone
};
// A main page (1..9) without a phase-2 screen falls back to the Tuner page
// while no main page is up yet (last_main_page == 0, g_lastMainPage) and is
// ignored once one is; MirrorLink (10) and the other pages without a phase-2
// screen are ignored. *show_page is written only for show / fall back.
Phase2PageAction ResolvePhase2Page(int page_id, int last_main_page, int* show_page);

// The start-up gate of the RUN / TEMP_ON / IGN branch of PA_OUT_MODE_CHG
// (VPowerProxy::RecieveVmfMsg 0x0005bab4, LAB_0005d7e4 and the audio-mode
// check after it), lifted out so the boot ordering can be host-tested.
struct PowerRunFlags {
    uint8_t startup_state;  // VPowerProxy +0x06: 1 = WS_IDLE / not yet started, 2 = startup done
    uint8_t hmi_run;        // VPowerProxy +0x04
    uint8_t running_init;   // VPowerProxy +0x16
};
enum PowerRunAction {
    kPowerRunIgnored = 0,     // dropped; flags unchanged
    kPowerRunNoAudioYet = 1,  // hmi_run is now 1, but no audio mode: no page
    kPowerRunShowMainPage = 2 // build the audio connection, recover the main page
};
// Applies the gate to `flags` (hmi_run / running_init) and says what follows.
PowerRunAction EvaluatePowerRunMode(PowerRunFlags* flags, uint8_t audio_mode);

// pwr_agent wake-state notify (group 0x6e sub 200, data[0]) in
// VPowerProxy::RecieveVmfMsg 0x0005bab4.
enum { kWakeStateIdle = 1, kWakeStateRunning = 2 };
// Applies WS_IDLE / WS_RUNNING to `flags`; true for WS_RUNNING, whose cascade
// re-reads the App-Connect PSS coding (0x003476a8) and asks the VIP for its
// audio mode (VAudioProxy::OnGetAudioModeFromVIP 0x0004776c); the 0x81 answer
// leads to a fresh RUN, which sets hmi_run again.
bool ApplyPowerWakeState(PowerRunFlags* flags, uint8_t wake_state);
// VAudioProxy::OnGetAudioModeFromVIP 0x0004776c: group 0x8f, sub 0x27, length 1, payload 2.
extern const uint8_t kGetAudioModeFromVipMsg[5];

// PSS read reply (group 0x72, sub 0xc9) in VPSSProxy::RecieveVmfMsg
// 0x001c5a90: pData[0] = block, pData[1] = status.
enum { kPssReadReplySub = 0xc9, kPssBlkSystemSetupNew = 0x4b };
enum PssReadOutcome {
    kPssReadIgnored = 0,  // status 1, 4 or >= 10: no action
    kPssReadOk = 1,       // status 0
    kPssReadFailed = 2    // status with a bit in 0x3ec (2, 3, 5..9)
};
PssReadOutcome ClassifyPssReadStatus(uint8_t status);
// Region chosen when the HMI_SYSTEM_SETUP_PSS_NEWBLK read fails: 1 unless the
// App-Connect coding (VAppConnectProxy::GetRegionMode 0x00347498) is region
// mode 2, then 0xff on platform type 1 (VPowerProxy +0x83), else 0.
uint8_t SetupNewBlkFailedRegion(int region_mode, uint8_t platform_type);
// Region -> language as VDisplayProxy::SetRegion 0x000509b0 decides it;
// false when that region leaves the language unchanged. `country` is the BAP
// vehicle country (VBAPProxy 0x000b1fd4, 0 when not valid).
bool RegionToLanguage(uint8_t region, int region_mode, uint8_t country, int* lang);
// pss handle_pss_read_req (pss 0x9a78) answers status 10 while its NVM data
// is not loaded yet (DAT_0002b8b2 == 0: cleared by pss_nvm_mgr_cold_reset
// 0xb310, set once the VIP warm-RAM backup is processed, 0xb54c).
// DEVIATION: ours starts earlier than stock and can ask that early. Stock
// (VPSSProxy::RecieveVmfMsg at 0x001c5a90) ignores status 10, so ours
// re-sends the 0x4b read up to kSetupReadMaxRetries times, kSetupReadRetryMs
// apart (10 s in all, slow hardware included), per fresh request.
enum { kPssStatusNotReady = 10, kSetupReadMaxRetries = 50, kSetupReadRetryMs = 200 };
// True when a 0x4b read reply with `status` should be retried after
// `retries_done` re-sends. Name is ours.
bool ShouldRetrySetupRead(uint8_t status, int retries_done);
// Retry bookkeeping of one setup-read episode (VPSSProxy, VMF thread only).
// Names are ours.
struct SetupReadRetryState {
    int retries;   // re-sends scheduled since the last reset
    bool gave_up;  // the bound was reached (logged once)
};
enum SetupReadRetryStep {
    kSetupReadNoRetry = 0,     // nothing to do (a real answer, or already gave up)
    kSetupReadRetryFirst = 1,  // schedule a re-send; the first of this episode
    kSetupReadRetry = 2,       // schedule a re-send
    kSetupReadGiveUp = 3       // bound reached: log once, stop re-sending
};
// A fresh OnSendPSSReadSystemSetupStatusRequest starts a new episode.
void ResetSetupReadRetry(SetupReadRetryState* state);
// Next step for a 0x4b read reply with `status`. Any status but 10 ends the
// episode and resets the state, so a later status 10 retries again.
SetupReadRetryStep OnSetupReadReply(SetupReadRetryState* state, uint8_t status);
// VPSSProxy::OnSendPSSReadSystemSetupStatusRequest 0x001c3ce0.
extern const uint8_t kPssReadSystemSetupMsg[10];

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PROXY_DECODERS_H
