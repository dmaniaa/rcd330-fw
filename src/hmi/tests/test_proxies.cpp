// File name is ours; stock name unknown.
#include "control_logic/subsystem/proxy_decoders.h"
#include "tests/test_runner.h"

static void test_power_decoders() {
    PowerDevStatus dev = {0, 0};
    const uint8_t dev_bytes[5] = {0x05, 0x05, 0x00, 0x00, 0x00};  // CL_S=1, CL_15=1
    CHECK(DecodePowerDevStatus(dev_bytes, 5, &dev));
    CHECK_EQ(dev.acc_status, 1);
    CHECK_EQ(dev.ignition_status, 1);
    CHECK(!DecodePowerDevStatus(dev_bytes, 0, &dev));

    // 0x0005bab4: ignition = (b0 & 0x0c) >> 2, ACC = b0 & 0x03.
    const uint8_t ign_only[1] = {0x04};
    CHECK(DecodePowerDevStatus(ign_only, 1, &dev));
    CHECK_EQ(dev.acc_status, 0);
    CHECK_EQ(dev.ignition_status, 1);
    CHECK(!DecodePowerDevStatus(0, 5, &dev));
    CHECK(!DecodePowerDevStatus(dev_bytes, 5, 0));

    uint8_t mode = 0;
    const uint8_t mode_run[1] = {3};
    CHECK(DecodePowerModeChange(mode_run, 1, &mode));
    CHECK_EQ(mode, 3);
    CHECK(!DecodePowerModeChange(mode_run, 0, &mode));

    uint8_t audio_mode = 0;
    const uint8_t vip_radio[2] = {0x81, 0x10};
    CHECK(DecodeVipAudioMode(vip_radio, 2, &audio_mode));
    CHECK_EQ(audio_mode, 0x10);
    // Other 0x74:0x01 selectors (0x82 aux, 0x83 key, ...) are not audio mode.
    const uint8_t vip_aux[2] = {0x82, 0x01};
    CHECK(!DecodeVipAudioMode(vip_aux, 2, &audio_mode));
    CHECK(!DecodeVipAudioMode(vip_radio, 1, &audio_mode));
}

static void test_appconnect_pss_decoder() {
    AppConnectCoding ac = {1, 0, false, false, false, false};
    // pData[2] == 0 sets region_mode = pData[3], sub_mode = pData[5] (0x34ac5c).
    const uint8_t pkt0[6] = {0, 4, 0, 2, 0, 1};
    CHECK(DecodeAppConnectPss(pkt0, 6, &ac));
    CHECK_EQ(ac.region_mode, 2u);
    CHECK_EQ(ac.sub_mode, 1u);
    // pData[2] == 1 carries protocol availability flags (0x3474a8).
    const uint8_t pkt1[6] = {0, 5, 1, 1, 1, 1};
    CHECK(DecodeAppConnectPss(pkt1, 6, &ac));
    CHECK(ac.cp || ac.aa || ac.ml);
    CHECK(!DecodeAppConnectPss(pkt0, 3, &ac));

    // Full flag record: 0x3474a8 reads (pData + 3)[2,3,4,10] = cp, cl, ml, aa.
    AppConnectCoding full = {1, 0, false, false, false, false};
    const uint8_t pkt_full[14] = {0, 12, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1};
    CHECK(DecodeAppConnectPss(pkt_full, 14, &full));
    CHECK(!full.cp);
    CHECK(full.cl);
    CHECK(full.ml);
    CHECK(full.aa);
    CHECK_EQ(full.region_mode, 1u);  // flag record leaves the region alone

    // Unknown selector: rejected, state untouched.
    const uint8_t pkt_bad[6] = {0, 4, 7, 2, 0, 1};
    CHECK(!DecodeAppConnectPss(pkt_bad, 6, &full));
    CHECK_EQ(full.region_mode, 1u);
}

static void test_bap_and_audio_decoders() {
    BapExtTemp env = {0, 0, true, false};
    // 20.5 C => (20.5 + 50) * 2 = 141; unit=0 (C), valid=0, night=1.
    const uint8_t a2_05[7] = {100, 141, 0, 0, 0, 1, 0};
    CHECK(DecodeBapEnvStatus(a2_05, 7, &env));
    CHECK_EQ(env.temp_half_c, 41);
    CHECK_EQ(env.unit_f, 0);
    CHECK(!env.invalid);
    CHECK(env.night_mode);
    // 0x000b9970: pData[3] == 1 or a zero raw temperature marks it invalid.
    const uint8_t a2_05_bad[7] = {100, 141, 0, 1, 0, 0, 0};
    CHECK(DecodeBapEnvStatus(a2_05_bad, 7, &env));
    CHECK(env.invalid);
    CHECK(!env.night_mode);
    CHECK(!DecodeBapEnvStatus(a2_05, 6, &env));

    AudioVolumeStatus av = {0, 0, 0, 0, 0, false};
    const uint8_t vol[6] = {0x00, 12, 12, 10, 8, 9};
    CHECK(DecodeAudioVolumeOrMute(vol, 6, &av));
    CHECK_EQ(av.active, 12);
    const uint8_t mute[5] = {0x04, 1, 1, 0, 0};
    CHECK(DecodeAudioVolumeOrMute(mute, 5, &av));
    CHECK(av.muted);
    CHECK_EQ(av.active, 12);  // mute status keeps the last volume
    const uint8_t other[2] = {0x02, 30};
    CHECK(!DecodeAudioVolumeOrMute(other, 2, &av));
    CHECK(!DecodeAudioVolumeOrMute(vol, 3, &av));
}

static void test_display_page_tables() {
    // 0x003f3ac4: audio source 3..16 -> logical main page, else Tuner (1).
    CHECK_EQ(MainPageForAudioSource(0), 1);
    CHECK_EQ(MainPageForAudioSource(2), 1);
    CHECK_EQ(MainPageForAudioSource(3), 2);
    CHECK_EQ(MainPageForAudioSource(9), 4);
    CHECK_EQ(MainPageForAudioSource(13), 10);
    CHECK_EQ(MainPageForAudioSource(14), 1);
    CHECK_EQ(MainPageForAudioSource(16), 2);
    CHECK_EQ(MainPageForAudioSource(17), 1);
    CHECK_EQ(MainPageForAudioSource(-5), 1);

    // 0x003f3850: logical page -> IGDT screen id.
    CHECK_EQ(ScreenForPage(1), 0xf9);   // Tuner
    CHECK_EQ(ScreenForPage(2), 0x52);   // Media overview
    CHECK_EQ(ScreenForPage(9), 0x04);   // App-Connect idle
    CHECK_EQ(ScreenForPage(0x14), 0x59);
    CHECK_EQ(ScreenForPage(0x15), 0x00);  // past the table
    CHECK_EQ(ScreenForPage(-1), 0x00);
    // InsertPage must not force-show screen 0 for phase-3 / unmapped pages.
    // Only pages whose screen VPageManager builds in phase 2 count.
    CHECK(PageHasPhase2Screen(1));      // 0xf9 Tuner
    CHECK(PageHasPhase2Screen(9));      // 0x04 App-Connect idle
    CHECK(!PageHasPhase2Screen(2));     // 0x52 Media: phase 3
    CHECK(!PageHasPhase2Screen(8));     // 0x02 App-Connect menu: phase 3
    CHECK(!PageHasPhase2Screen(0x14));  // 0x59: phase 3
    CHECK(!PageHasPhase2Screen(10));    // MirrorLink, MainPageForAudioSource(13)
    CHECK(!PageHasPhase2Screen(MainPageForAudioSource(13)));
    CHECK(!PageHasPhase2Screen(0));
    CHECK(!PageHasPhase2Screen(0x15));
    CHECK(!PageHasPhase2Screen(-1));
}

static void test_phase3_main_page_falls_back_to_tuner() {
    // The screens VPageManager::ShowPage builds in phase 2.
    CHECK(ScreenHasPhase2Page(0xf9));
    CHECK(ScreenHasPhase2Page(0x04));
    CHECK(ScreenHasPhase2Page(0x03));
    CHECK(!ScreenHasPhase2Page(0x52));
    CHECK(!ScreenHasPhase2Page(0x02));
    CHECK(!ScreenHasPhase2Page(0x00));

    // Boot with media as the last source: the 0x81 audio mode makes page 2
    // the main page, RecoverSetupMainPage asks for it with no main page up.
    // DEVIATION: phase 2 shows the tuner shell instead of nothing.
    const int boot_page = MainPageForAudioSource(3);
    CHECK_EQ(boot_page, 2);
    int show = -1;
    CHECK_EQ(ResolvePhase2Page(boot_page, 0, &show), kPhase2PageFallBack);
    CHECK_EQ(show, 1);
    CHECK_EQ(ScreenForPage(show), 0xf9);
    CHECK(PageHasPhase2Screen(show));
    // App-Connect menu (8) at boot: same fallback.
    show = -1;
    CHECK_EQ(ResolvePhase2Page(8, 0, &show), kPhase2PageFallBack);
    CHECK_EQ(show, 1);

    // With a main page up (tuner shown, g_lastMainPage 1) the request is
    // dropped: no page change, no state change.
    show = -1;
    CHECK_EQ(ResolvePhase2Page(2, 1, &show), kPhase2PageIgnore);
    CHECK_EQ(show, -1);
    // Phase-2 pages show as asked.
    CHECK_EQ(ResolvePhase2Page(1, 0, &show), kPhase2PageShow);
    CHECK_EQ(show, 1);
    CHECK_EQ(ResolvePhase2Page(9, 1, &show), kPhase2PageShow);
    CHECK_EQ(show, 9);
    // MirrorLink (10) keeps its own branch: dropped even at boot.
    show = -1;
    CHECK_EQ(ResolvePhase2Page(10, 0, &show), kPhase2PageIgnore);
    CHECK_EQ(show, -1);
    // Non-main pages without a phase-2 screen never fall back.
    CHECK_EQ(ResolvePhase2Page(0x0c, 0, &show), kPhase2PageIgnore);
    CHECK_EQ(ResolvePhase2Page(0, 0, &show), kPhase2PageIgnore);
    CHECK_EQ(ResolvePhase2Page(0x15, 0, &show), kPhase2PageIgnore);
    CHECK_EQ(show, -1);
    CHECK_EQ(ResolvePhase2Page(2, 0, 0), kPhase2PageIgnore);
}

static void test_power_run_boot_order() {
    // main(): SetStartupDone(1) before VmfThread::start() (DEVIATION).
    PowerRunFlags f = {1, 0, 0};
    // 1. An early PA_OUT_MODE_CHG RUN, before any 0x81 audio mode: dropped.
    CHECK_EQ(EvaluatePowerRunMode(&f, 0), kPowerRunIgnored);
    CHECK_EQ(f.hmi_run, 0);
    // 2. 0x81 audio mode: tuner channel set, SetStartupDone(2), and
    //    GetPwrCurStateToPowerAgent asks for the power mode again.
    f.startup_state = 2;
    // 3. The RUN reply shows the main page.
    CHECK_EQ(EvaluatePowerRunMode(&f, 1), kPowerRunShowMainPage);
    CHECK_EQ(f.hmi_run, 1);
    // A further RUN is a repeat until OnSaveAudioModeToVIP sets running_init 2.
    CHECK_EQ(EvaluatePowerRunMode(&f, 1), kPowerRunIgnored);
    f.running_init = 2;
    CHECK_EQ(EvaluatePowerRunMode(&f, 1), kPowerRunShowMainPage);
    CHECK_EQ(f.running_init, 1);

    // Stock order (startup state still 0 when the early RUN arrives): the
    // RUN sets hmi_run without a page and the reply is dropped as a repeat.
    // This is the boot stall the main() ordering avoids.
    PowerRunFlags stock = {0, 0, 0};
    CHECK_EQ(EvaluatePowerRunMode(&stock, 0), kPowerRunNoAudioYet);
    CHECK_EQ(stock.hmi_run, 1);
    stock.startup_state = 2;
    CHECK_EQ(EvaluatePowerRunMode(&stock, 1), kPowerRunIgnored);

    // WS_IDLE (startup state 1) drops RUN; NULL flags are refused.
    PowerRunFlags idle = {1, 0, 0};
    CHECK_EQ(EvaluatePowerRunMode(&idle, 1), kPowerRunIgnored);
    CHECK_EQ(EvaluatePowerRunMode(0, 1), kPowerRunIgnored);
}

static void test_power_wake_state_cycle_recovers_run() {
    // pwr_agent's WS_IDLE -> WS_RUNNING cycle can arrive after the boot page
    // is up (our hmi starts faster than stock). Both states clear hmi_run, so
    // without a new RUN every key is dropped (VControlTask 0x000322bc).
    PowerRunFlags f = {2, 1, 1};  // boot page shown
    CHECK(!ApplyPowerWakeState(&f, kWakeStateIdle));
    CHECK_EQ(f.startup_state, 1);
    CHECK_EQ(f.hmi_run, 0);
    CHECK_EQ(f.running_init, 0);
    // WS_RUNNING asks the VIP for its audio mode (0x0004776c) ...
    CHECK(ApplyPowerWakeState(&f, kWakeStateRunning));
    CHECK_EQ(f.hmi_run, 0);
    CHECK_EQ(f.running_init, 1);
    // ... whose 0x81 answer sets startup state 2 and re-requests the power
    // state; the RUN reply then shows the main page and sets hmi_run again.
    f.startup_state = 2;
    CHECK_EQ(EvaluatePowerRunMode(&f, 1), kPowerRunShowMainPage);
    CHECK_EQ(f.hmi_run, 1);
    // Unknown wake states only clear hmi_run (as stock); NULL flags are refused.
    PowerRunFlags g = {2, 1, 1};
    CHECK(!ApplyPowerWakeState(&g, 3));
    CHECK_EQ(g.startup_state, 2);
    CHECK_EQ(g.running_init, 1);
    CHECK_EQ(g.hmi_run, 0);
    CHECK(!ApplyPowerWakeState(0, kWakeStateRunning));
    // OnGetAudioModeFromVIP: group 0x8f, sub 0x27, length 1, payload 2.
    const uint8_t expect[5] = {0x8f, 0x27, 0x00, 0x01, 0x02};
    for (int i = 0; i < 5; ++i) CHECK_EQ(kGetAudioModeFromVipMsg[i], expect[i]);
}

static void test_setup_newblk_language_decision() {
    // VPSSProxy::RecieveVmfMsg 0x001c5a90, block 0x4b: status 0 is a good
    // read, statuses with a bit in 0x3ec fail, 1 / 4 / >= 10 are ignored.
    CHECK_EQ(ClassifyPssReadStatus(0), kPssReadOk);
    CHECK_EQ(ClassifyPssReadStatus(1), kPssReadIgnored);
    CHECK_EQ(ClassifyPssReadStatus(4), kPssReadIgnored);
    CHECK_EQ(ClassifyPssReadStatus(10), kPssReadIgnored);
    const uint8_t failed[7] = {2, 3, 5, 6, 7, 8, 9};
    for (int i = 0; i < 7; ++i) CHECK_EQ(ClassifyPssReadStatus(failed[i]), kPssReadFailed);
    // Read failure: region 1 unless the App-Connect coding is region mode 2;
    // then 0xff on platform 1, else 0.
    CHECK_EQ(SetupNewBlkFailedRegion(1, 1), 1);
    CHECK_EQ(SetupNewBlkFailedRegion(0, 0), 1);
    CHECK_EQ(SetupNewBlkFailedRegion(2, 1), 0xff);
    CHECK_EQ(SetupNewBlkFailedRegion(2, 0), 0);
    CHECK_EQ(SetupNewBlkFailedRegion(2, 2), 0);
    // Region -> language (0x000509b0): 0..4 map to themselves.
    int lang = -1;
    for (int r = 0; r <= 4; ++r) {
        CHECK(RegionToLanguage(static_cast<uint8_t>(r), 1, 0, &lang));
        CHECK_EQ(lang, r);
    }
    // Other regions except 0xff keep the language.
    CHECK(!RegionToLanguage(5, 2, 0, &lang));
    CHECK(!RegionToLanguage(0xfe, 2, 0, &lang));
    // 0xff without coding region 2: country 9 -> 1 (zh_CN), else 0 (en_US).
    CHECK(RegionToLanguage(0xff, 1, 9, &lang));
    CHECK_EQ(lang, 1);
    CHECK(RegionToLanguage(0xff, 1, 0, &lang));
    CHECK_EQ(lang, 0);
    // 0xff with coding region 2: country 0x0d -> 3, 0x15 / 6 -> 2,
    // 5 / 0x13 -> 4, else 0.
    const uint8_t country[7] = {0x0d, 0x15, 0x06, 0x05, 0x13, 0x09, 0x00};
    const int expect[7] = {3, 2, 2, 4, 4, 0, 0};
    for (int i = 0; i < 7; ++i) {
        CHECK(RegionToLanguage(0xff, 2, country[i], &lang));
        CHECK_EQ(lang, expect[i]);
    }
    CHECK(!RegionToLanguage(0, 1, 0, 0));
    // OnSendPSSReadSystemSetupStatusRequest 0x001c3ce0: group 0x8c, sub 0,
    // length 6, read block 0x4b of PSS group 0x72.
    const uint8_t req[10] = {0x8c, 0x00, 0x00, 0x06, 0x72, 0x4b, 0x00, 0x00, 0x00, 0x14};
    for (int i = 0; i < 10; ++i) CHECK_EQ(kPssReadSystemSetupMsg[i], req[i]);
}

static void test_setup_read_retry_on_not_ready() {
    // pss answers status 10 while its NVM is not loaded yet (DAT_0002b8b2
    // == 0 in handle_pss_read_req, pss 0x9a78). DEVIATION: ours re-sends
    // the 0x4b read, at most kSetupReadMaxRetries times per fresh request.
    CHECK_EQ(kPssStatusNotReady, 10);
    CHECK_EQ(kSetupReadMaxRetries, 50);   // 50 x 200 ms = 10 s
    CHECK_EQ(kSetupReadRetryMs, 200);
    CHECK(ShouldRetrySetupRead(kPssStatusNotReady, 0));
    CHECK(ShouldRetrySetupRead(kPssStatusNotReady, kSetupReadMaxRetries - 1));
    CHECK(!ShouldRetrySetupRead(kPssStatusNotReady, kSetupReadMaxRetries));
    // Real answers (good read, failed read, ignored statuses) never retry.
    const uint8_t final_status[6] = {0, 1, 4, 8, 9, 11};
    for (int i = 0; i < 6; ++i) CHECK(!ShouldRetrySetupRead(final_status[i], 0));
}

static void test_setup_read_retry_state_resets() {
    SetupReadRetryState st;
    st.retries = 7;
    st.gave_up = true;
    ResetSetupReadRetry(&st);
    CHECK_EQ(st.retries, 0);
    CHECK(!st.gave_up);

    // First status 10 starts retrying (logged), later ones keep retrying.
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadRetryFirst);
    CHECK_EQ(st.retries, 1);
    for (int i = 1; i < kSetupReadMaxRetries; ++i) {
        CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadRetry);
    }
    CHECK_EQ(st.retries, kSetupReadMaxRetries);
    // The bound is reached: give up once, with the real retry count.
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadGiveUp);
    CHECK(st.gave_up);
    CHECK_EQ(st.retries, kSetupReadMaxRetries);
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadNoRetry);

    // A fresh OnSendPSSReadSystemSetupStatusRequest starts over.
    ResetSetupReadRetry(&st);
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadRetryFirst);
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadRetry);

    // Any other reply ends the episode and resets: a later status 10 (pss
    // restarted) retries from the start again.
    CHECK_EQ(OnSetupReadReply(&st, 8), kSetupReadNoRetry);
    CHECK_EQ(st.retries, 0);
    CHECK(!st.gave_up);
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadRetryFirst);
    for (int i = 1; i < kSetupReadMaxRetries; ++i) OnSetupReadReply(&st, kPssStatusNotReady);
    CHECK_EQ(OnSetupReadReply(&st, kPssStatusNotReady), kSetupReadGiveUp);
    CHECK_EQ(OnSetupReadReply(&st, 0), kSetupReadNoRetry);
    CHECK(!st.gave_up);
    CHECK_EQ(st.retries, 0);

    CHECK_EQ(OnSetupReadReply(0, kPssStatusNotReady), kSetupReadNoRetry);
    ResetSetupReadRetry(0);
}

int main() {
    RUN_TEST(test_power_decoders);
    RUN_TEST(test_appconnect_pss_decoder);
    RUN_TEST(test_bap_and_audio_decoders);
    RUN_TEST(test_display_page_tables);
    RUN_TEST(test_phase3_main_page_falls_back_to_tuner);
    RUN_TEST(test_power_run_boot_order);
    RUN_TEST(test_power_wake_state_cycle_recovers_run);
    RUN_TEST(test_setup_newblk_language_decision);
    RUN_TEST(test_setup_read_retry_on_not_ready);
    RUN_TEST(test_setup_read_retry_state_resets);
    return test_summary();
}
