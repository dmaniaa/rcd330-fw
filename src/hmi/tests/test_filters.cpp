// File name is ours; stock name unknown.
// Host tests for the notify filter chain (control_logic/mode): VBaseFilter
// routing, VPowerNotifyFilter, VIPCLNotifyFilter with its VHMIKey state
// machine, and VDisplayCmdFilter. Proxies, VPageManager and VControlTask are
// replaced by MockActions (IFilterActions).
#include "control_logic/mode/display/vdisplaycmdfilter.h"
#include "control_logic/mode/ifilteractions.h"
#include "control_logic/mode/ipcl/vhmikey.h"
#include "control_logic/mode/ipcl/vipclnotifyfilter.h"
#include "control_logic/mode/power/vpowernotifyfilter.h"
#include "control_logic/mode/vbasefilter.h"
#include "control_logic/mode/vprehandlefilter.h"
#include "tests/test_runner.h"

#include <vector>

struct MockActions : public IFilterActions {
    std::vector<int> main_pages;
    std::vector<int> inserted_pages;
    std::vector<int> closed_pops;
    std::vector<int> audio_modes;
    std::vector<int> key_events;  // (key << 8) | state
    std::vector<int> bap_keys;    // (key << 8) | state
    std::vector<int> key_timer;   // ms for start, -1 for stop
    int tuner_queries;
    int hmi_on_notifies;
    int backlight_updates;
    int cur_display_page;
    bool igdt_shown;
    int cur_screen;
    int base_screen;
    std::vector<int> shown_screens;
    std::vector<int> stacked_screens;
    int audio_mode;
    int tuner_band;
    int tuner_state;
    bool hmi_on;
    bool temp_protect;
    int aux_mode;
    int region_mode;
    int app_device_type;
    int media_flags;
    int mirrorlink_state;
    int active_key;
    bool multi_permit;

    MockActions()
        : tuner_queries(0), hmi_on_notifies(0), backlight_updates(0), cur_display_page(0),
          igdt_shown(true), cur_screen(0xf9), base_screen(0xf9), audio_mode(1), tuner_band(1),
          tuner_state(0), hmi_on(true), temp_protect(false), aux_mode(0), region_mode(2),
          app_device_type(0), media_flags(0), mirrorlink_state(0), active_key(0xffff),
          multi_permit(true) {}

    static bool Has(const std::vector<int>& v, int x) {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == x) return true;
        }
        return false;
    }

    void SetMainPage(int audio_src, int force) { main_pages.push_back((audio_src << 8) | force); }
    void InsertPage(int page_id) { inserted_pages.push_back(page_id); }
    void ClosePopPage(uint8_t pop_id) { closed_pops.push_back(pop_id); }
    void NotifyHmiOn() { ++hmi_on_notifies; }
    int GetCurDisplayPage() const { return cur_display_page; }
    bool IsIgdtShown() const { return igdt_shown; }
    void UpdateBacklight() { ++backlight_updates; }
    int GetCurrentScreen() const { return cur_screen; }
    int GetBaseScreen() const { return base_screen; }
    bool IsScreenShown(uint8_t scr) const { return Has(shown_screens, scr); }
    bool IsScreenInStack(uint8_t scr) const { return Has(stacked_screens, scr); }
    int GetAudioMode() const { return audio_mode; }
    void SetAudioMode(int mode) { audio_modes.push_back(mode); }
    void QueryTunerInfo() { ++tuner_queries; }
    int GetTunerBand() const { return tuner_band; }
    int GetTunerState() const { return tuner_state; }
    bool IsHMIOn() const { return hmi_on; }
    bool IsTempProtect() const { return temp_protect; }
    int GetAuxMode() const { return aux_mode; }
    int GetRegionMode() const { return region_mode; }
    int GetAppDeviceType() const { return app_device_type; }
    int GetMediaDeviceFlags() const { return media_flags; }
    int GetMirrorLinkState() const { return mirrorlink_state; }
    void StartKeyTimer(int ms) { key_timer.push_back(ms); }
    void StopKeyTimer() { key_timer.push_back(-1); }
    int GetActiveKeyCode() const { return active_key; }
    bool IsMultiHardkeyPermit() const { return multi_permit; }
    void NotifyDisplayKey(uint8_t key, uint8_t state) { key_events.push_back((key << 8) | state); }
    void NotifyBapKey(uint8_t key, uint8_t state) { bap_keys.push_back((key << 8) | state); }
};

static FILTER_MSG MakeMsg(uint8_t valid, uint32_t type, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
    FILTER_MSG m;
    m.Reset();
    m.valid = valid;
    m.type = type;
    m.para[0] = p0;
    m.para[1] = p1;
    m.para[2] = p2;
    m.para[3] = p3;
    return m;
}

// Records every message it sees; returns it unchanged (base pass-through).
class RecordingFilter : public VBaseFilter {
public:
    RecordingFilter() : VBaseFilter(0) {}
    std::vector<FILTER_MSG> seen;
    virtual FILTER_MSG HandleMsg(const FILTER_MSG& msg) {
        seen.push_back(msg);
        return VBaseFilter::HandleMsg(msg);
    }
};

static void test_filter_chain_dedup() {
    VBaseFilter a(0), b(0);
    CHECK(a.AddNextFilter(&b));
    CHECK(!a.AddNextFilter(&b));  // duplicate rejected like 0x00039780
    CHECK(!a.AddNextFilter(0));
    CHECK_EQ(a.NextCount(), 1u);
}

static void test_dispatch_stops_on_invalid_and_forwards_result() {
    RecordingFilter head, tail;
    CHECK(head.AddNextFilter(&tail));

    // type 0 is rejected by HandleMsg (valid forced to 0): nothing forwarded.
    head.DispatchMsg(MakeMsg(2, 0, 2, 5, 0x10, 0));
    CHECK_EQ(head.seen.size(), 1u);
    CHECK_EQ(tail.seen.size(), 0u);

    // A notify for a subsystem nobody handles passes through unchanged.
    head.DispatchMsg(MakeMsg(2, 3, 0x0d, 1, 2, 3));
    CHECK_EQ(tail.seen.size(), 1u);
    CHECK_EQ(tail.seen[0].para[0], 0x0d);
    CHECK_EQ(tail.seen[0].valid, 2);

    // valid == 0 on input is forwarded nowhere either.
    head.DispatchMsg(MakeMsg(0, 3, 0x0d, 1, 2, 3));
    CHECK_EQ(tail.seen.size(), 1u);
}

static void test_power_audio_mode_status() {
    MockActions act;
    VPowerNotifyFilter f(&act);

    // 0x00046054: para[2] >> 4 == 1 (default branch) -> audio mode 1, main
    // page via SetMainPage(1, 1) since para[2] & 0xf == 0, then tuner queries.
    FILTER_MSG out = f.HandleMsg(MakeMsg(2, 3, 2, 5, 0x10, 0));
    CHECK_EQ(act.audio_modes.size(), 1u);
    CHECK_EQ(act.audio_modes[0], 1);
    CHECK_EQ(act.main_pages.size(), 1u);
    CHECK_EQ(act.main_pages[0], (1 << 8) | 1);
    CHECK_EQ(act.tuner_queries, 1);
    CHECK_EQ(out.valid, 2);
    CHECK_EQ(out.para[2], 0x10);

    // 0x23: mode 2, explicit page 3 -> InsertPage(3); mode < 2 index -> query.
    f.HandleMsg(MakeMsg(2, 3, 2, 5, 0x23, 0));
    CHECK_EQ(act.audio_modes[1], 2);
    CHECK_EQ(act.inserted_pages.size(), 1u);
    CHECK_EQ(act.inserted_pages[0], 3);
    CHECK_EQ(act.tuner_queries, 2);

    // 0x50: mode 5, no tuner query (index 4 >= 2).
    f.HandleMsg(MakeMsg(2, 3, 2, 5, 0x50, 0));
    CHECK_EQ(act.audio_modes[2], 5);
    CHECK_EQ(act.main_pages[1], (5 << 8) | 1);
    CHECK_EQ(act.tuner_queries, 2);

    // 0x40 without aux/media flag falls back to mode 1; with aux it is 4.
    f.HandleMsg(MakeMsg(2, 3, 2, 5, 0x40, 0));
    CHECK_EQ(act.audio_modes[3], 1);
    act.aux_mode = 1;
    f.HandleMsg(MakeMsg(2, 3, 2, 5, 0x40, 0));
    CHECK_EQ(act.audio_modes[4], 4);

    // 0xd0 (MirrorLink) only with MirrorLink state 2, else mode 1.
    f.HandleMsg(MakeMsg(2, 3, 2, 5, 0xd0, 0));
    CHECK_EQ(act.audio_modes[5], 1);
}

static void test_power_eject_and_unhandled_subs() {
    MockActions act;
    VPowerNotifyFilter f(&act);
    // 0x00045ec4: eject 0 -> media command {0x0b, 9}, 1 -> {0x0b, 10}.
    FILTER_MSG out = f.HandleMsg(MakeMsg(2, 3, 2, 4, 0, 0));
    CHECK_EQ(out.valid, 1);
    CHECK_EQ(out.type, 2u);
    CHECK_EQ(out.para[0], 0x0b);
    CHECK_EQ(out.para[1], 9);
    out = f.HandleMsg(MakeMsg(2, 3, 2, 4, 1, 0));
    CHECK_EQ(out.para[1], 10);
    out = f.HandleMsg(MakeMsg(2, 3, 2, 4, 7, 0));
    CHECK_EQ(out.valid, 2);
    CHECK_EQ(out.para[0], 2);

    // VControlTask::PowersubsystemNotifyMsg sends {2, 0} for 0x74:0x81:
    // sub 0 has no handler and passes through without side effects.
    out = f.HandleMsg(MakeMsg(2, 3, 2, 0, 0, 0));
    CHECK_EQ(out.valid, 2);
    CHECK_EQ(act.audio_modes.size(), 0u);
    CHECK_EQ(act.main_pages.size(), 0u);
}

static void test_hmikey_short_long_release() {
    MockActions act;
    VIPCLNotifyFilter f(&act);
    VHMIKey& keys = f.Keys();
    (void)keys;

    // Press: callback with old state 1 -> NotifyDisplayKey(key, 3); timer 1000.
    FILTER_MSG out = f.HandleMsg(MakeMsg(2, 3, 4, 1, 10, 1));
    CHECK_EQ(out.valid, 0);
    CHECK_EQ(act.key_timer.size(), 1u);
    CHECK_EQ(act.key_timer[0], 1000);
    CHECK_EQ(act.key_events.size(), 1u);
    CHECK_EQ(act.key_events[0], (10 << 8) | 3);

    // Release before 2000 ms: short release -> (key, 4) + BAP (key, 4).
    out = f.HandleMsg(MakeMsg(2, 3, 4, 1, 10, 2));
    CHECK_EQ(out.valid, 0);
    CHECK_EQ(act.key_timer[1], -1);
    CHECK_EQ(act.key_events.size(), 2u);
    CHECK_EQ(act.key_events[1], (10 << 8) | 4);
    CHECK_EQ(act.bap_keys.size(), 1u);
    CHECK_EQ(act.bap_keys[0], (10 << 8) | 4);

    // Long press: two 1000 ms ticks reach the 2000 ms threshold -> (key, 5),
    // then release -> (key, 6).
    f.HandleMsg(MakeMsg(2, 3, 4, 1, 1, 1));
    f.KeyTimeout(1000);
    CHECK_EQ(act.key_events.size(), 3u);
    f.KeyTimeout(1000);
    CHECK_EQ(act.key_events.size(), 4u);
    CHECK_EQ(act.key_events[3], (1 << 8) | 5);
    f.HandleMsg(MakeMsg(2, 3, 4, 1, 1, 2));
    CHECK_EQ(act.key_events[4], (1 << 8) | 6);

    // A key VIPCLNotifyFilter does not register (0x0c) is ignored.
    f.HandleMsg(MakeMsg(2, 3, 4, 1, 0x0c, 1));
    CHECK_EQ(act.key_events.size(), 5u);
}

static void test_ipcl_app_and_radio_on_reposted_release() {
    MockActions act;
    VIPCLNotifyFilter f(&act);

    // VDisplayTask re-posts (key, 4) as {2, 4, {4, 1, key, 4}} when no IGDT
    // page consumed it; the active driver key must match.
    act.active_key = 10;
    act.cur_screen = 0xf9;
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 10, 4));
    CHECK_EQ(act.inserted_pages.size(), 1u);
    CHECK_EQ(act.inserted_pages[0], 9);  // App-Connect, no device -> screen 0x04

    // An App-Connect screen in the stack routes to page 8 instead.
    act.stacked_screens.push_back(0x55);
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 10, 4));
    CHECK_EQ(act.inserted_pages[1], 8);
    act.stacked_screens.clear();

    // Region not decided (0) ignores APP.
    act.region_mode = 0;
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 10, 4));
    CHECK_EQ(act.inserted_pages.size(), 2u);
    act.region_mode = 2;

    // RADIO from the App-Connect screen in tuner audio mode: page 1 + queries.
    act.active_key = 1;
    act.cur_screen = 0x04;
    act.audio_mode = 1;
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 1, 4));
    CHECK_EQ(act.inserted_pages.size(), 3u);
    CHECK_EQ(act.inserted_pages[2], 1);
    CHECK_EQ(act.tuner_queries, 1);

    // Already on the tuner screen: nothing.
    act.cur_screen = 0xf9;
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 1, 4));
    CHECK_EQ(act.inserted_pages.size(), 3u);

    // Non-tuner audio mode: RADIO asks the audio command filter for a source
    // switch {1, 2, {2, band<3 ? 1 : 2}} and shows no page itself.
    act.cur_screen = 0x04;
    act.audio_mode = 5;
    act.tuner_band = 3;
    FILTER_MSG out = f.HandleMsg(MakeMsg(2, 4, 4, 1, 1, 4));
    CHECK_EQ(out.valid, 1);
    CHECK_EQ(out.type, 2u);
    CHECK_EQ(out.para[0], 2);
    CHECK_EQ(out.para[1], 2);
    CHECK_EQ(act.inserted_pages.size(), 3u);

    // Mismatching active key: the handler is not called.
    act.audio_mode = 1;
    act.active_key = 10;
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 1, 4));
    CHECK_EQ(act.inserted_pages.size(), 3u);

    // Before the first igdt_show (VDisplayProxy +0xc8 == 0) keys are dropped.
    act.active_key = 1;
    act.igdt_shown = false;
    out = f.HandleMsg(MakeMsg(2, 4, 4, 1, 1, 4));
    CHECK_EQ(out.valid, 0);
    CHECK_EQ(act.inserted_pages.size(), 3u);
}

// Review focus 5: MEDIA, VOICE, SETUP, PHONE, EJECT and unknown codes are
// logged once and dropped without page changes; APP/RADIO still work after.
static void test_ipcl_phase3_keys_are_safe() {
    MockActions act;
    VIPCLNotifyFilter f(&act);
    act.cur_screen = 0x04;
    const uint8_t phase3[] = {2, 3, 4, 5, 9, 0x0b, 0x63};
    for (int round = 0; round < 2; ++round) {
        for (size_t i = 0; i < sizeof(phase3); ++i) {
            act.active_key = phase3[i];
            f.HandleMsg(MakeMsg(2, 3, 4, 1, phase3[i], 1));
            f.HandleMsg(MakeMsg(2, 3, 4, 1, phase3[i], 2));
            f.HandleMsg(MakeMsg(2, 4, 4, 1, phase3[i], 4));
            f.HandleMsg(MakeMsg(2, 4, 4, 1, phase3[i], 6));
        }
    }
    CHECK_EQ(act.inserted_pages.size(), 0u);
    CHECK_EQ(act.main_pages.size(), 0u);

    act.active_key = 1;
    act.audio_mode = 1;
    f.HandleMsg(MakeMsg(2, 4, 4, 1, 1, 4));
    CHECK_EQ(act.inserted_pages.size(), 1u);
    CHECK_EQ(act.inserted_pages[0], 1);
}

static void test_tune_down_long_release_requests_seek() {
    MockActions act;
    VIPCLNotifyFilter f(&act);
    // 0x00041990: state 4 in tuner mode -> AMFM command {1, 2, {0x0c, 7}}.
    FILTER_MSG out = f.HandleMsg(MakeMsg(2, 4, 4, 1, 8, 4));
    CHECK_EQ(out.valid, 1);
    CHECK_EQ(out.type, 2u);
    CHECK_EQ(out.para[0], 0x0c);
    CHECK_EQ(out.para[1], 7);
    // TUNE_UP passes the message on unchanged.
    out = f.HandleMsg(MakeMsg(2, 4, 4, 1, 7, 4));
    CHECK_EQ(out.valid, 2);
    CHECK_EQ(out.para[0], 4);
}

static void test_display_cmd_filter() {
    MockActions act;
    VDisplayCmdFilter f(&act);
    // 0x00040e38: para[0] 7 = op list of para[1] entries: 2 = InsertPage,
    // 6 = ClosePage(popup).
    FILTER_MSG cmd = MakeMsg(1, 2, 7, 2, 2, 9);
    cmd.para[4] = 6;
    cmd.para[5] = 0x2a;
    FILTER_MSG out = f.HandleMsg(cmd);
    CHECK_EQ(out.valid, 1);
    CHECK_EQ(act.inserted_pages.size(), 1u);
    CHECK_EQ(act.inserted_pages[0], 9);
    CHECK_EQ(act.closed_pops.size(), 1u);
    CHECK_EQ(act.closed_pops[0], 0x2a);
    CHECK_EQ(act.hmi_on_notifies, 1);

    // An op list running past the end of para is cut off, not over-read.
    FILTER_MSG bad = MakeMsg(1, 2, 7, 200, 2, 1);
    f.HandleMsg(bad);
    CHECK(act.inserted_pages.size() >= 2u);

    // Notifies are not commands: untouched.
    f.HandleMsg(MakeMsg(2, 3, 7, 1, 2, 9));
    CHECK(act.inserted_pages.size() < 40u);
}

static void test_chain_power_to_prehandle_to_display() {
    MockActions act;
    VPowerNotifyFilter power(&act);
    VPrehandleFilter pre(&act);
    VDisplayCmdFilter display(&act);
    CHECK(power.AddNextFilter(&pre));
    CHECK(pre.AddNextFilter(&display));
    power.DispatchMsg(MakeMsg(2, 3, 2, 5, 0x10, 0));
    // Handled once in VPowerNotifyFilter, not again downstream.
    CHECK_EQ(act.audio_modes.size(), 1u);
    CHECK_EQ(act.main_pages.size(), 1u);
}

int main() {
    RUN_TEST(test_filter_chain_dedup);
    RUN_TEST(test_dispatch_stops_on_invalid_and_forwards_result);
    RUN_TEST(test_power_audio_mode_status);
    RUN_TEST(test_power_eject_and_unhandled_subs);
    RUN_TEST(test_hmikey_short_long_release);
    RUN_TEST(test_ipcl_app_and_radio_on_reposted_release);
    RUN_TEST(test_ipcl_phase3_keys_are_safe);
    RUN_TEST(test_tune_down_long_release_requests_seek);
    RUN_TEST(test_display_cmd_filter);
    RUN_TEST(test_chain_power_to_prehandle_to_display);
    return test_summary();
}
