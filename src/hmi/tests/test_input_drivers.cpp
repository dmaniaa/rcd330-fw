// File name is ours; stock name unknown.
// Portable input decoders behind VKeyDriver (0x000aa77c), VKnobDriver
// (0x000aa3e0) and VMultiTouch (0x00023b14 / 0x00023cac / 0x00023d34).
#include "control_logic/input/input_decoders.h"
#include "service/tslib/tslib_api.h"
#include "tests/test_runner.h"

#include <stddef.h>
#include <string.h>
#include <unistd.h>

static void test_ts_sample_size_is_24() {
    // Review Focus 3: Visteon custom libts-1.0.so.0 writes 24-byte samples (0x23cac).
    CHECK_EQ(sizeof(ts_sample), 24u);
    CHECK_EQ(offsetof(ts_sample, pressure), 8u);
    CHECK_EQ(offsetof(ts_sample, id), 0x15u);
}

static void test_input_event_is_16_bytes() {
    CHECK_EQ(sizeof(InputEvent32), 16u);
    CHECK_EQ(offsetof(InputEvent32, type), 8u);
    CHECK_EQ(offsetof(InputEvent32, code), 10u);
    CHECK_EQ(offsetof(InputEvent32, value), 12u);
}

static void test_hardkey_decoding_and_bounds() {
    KeyFilterState st = {0, true, false};
    uint8_t code = 0, val = 0;
    bool toggle = false;

    // Non-EV_KEY and out-of-range codes (> 10) rejected.
    CHECK(!DecodeHardKeyEvent(st, true, 0, 10, 1, &code, &val, &toggle));
    CHECK(!DecodeHardKeyEvent(st, true, 1, 11, 1, &code, &val, &toggle));
    CHECK(!DecodeHardKeyEvent(st, true, 1, 0xffff, 1, &code, &val, &toggle));
    // STOCK-QUIRK: 0xaa77c only rejects code > 10 (unsigned), so KEY_RESERVED
    // (0) is forwarded like any other key.
    CHECK(DecodeHardKeyEvent(st, true, 1, 0, 0, &code, &val, &toggle));
    CHECK_EQ(code, 0);
    CHECK(!toggle);
    CHECK(st.multi_permit);

    // APP press locks multi_permit; unrelated key blocked until APP release.
    CHECK(DecodeHardKeyEvent(st, true, 1, 10, 1, &code, &val, &toggle));
    CHECK_EQ(code, 10);
    CHECK_EQ(val, 1);
    CHECK(toggle);
    CHECK(!st.multi_permit);
    CHECK_EQ(st.active_code, 10);

    CHECK(!DecodeHardKeyEvent(st, true, 1, 2, 1, &code, &val, &toggle));
    CHECK(!toggle);
    CHECK_EQ(st.active_code, 10);

    // APP release unlocks multi_permit.
    CHECK(DecodeHardKeyEvent(st, true, 1, 10, 0, &code, &val, &toggle));
    CHECK_EQ(code, 10);
    CHECK_EQ(val, 0);
    CHECK(toggle);
    CHECK(st.multi_permit);

    // TUNE_OK (code 6) tracks pressed state for VKnobDriver interlock.
    CHECK(DecodeHardKeyEvent(st, true, 1, 6, 1, &code, &val, &toggle));
    CHECK(st.tune_ok_down);
    CHECK(DecodeHardKeyEvent(st, true, 1, 6, 0, &code, &val, &toggle));
    CHECK(!st.tune_ok_down);
}

static void test_hardkey_radio_phone_pass_while_locked() {
    // 0xaa77c: with RADIO (1) or PHONE (5) held, the other one still posts
    // and leaves the lock and active code alone.
    KeyFilterState st = {0, true, false};
    uint8_t code = 0, val = 0;
    bool toggle = false;
    CHECK(DecodeHardKeyEvent(st, true, 1, 1, 1, &code, &val, &toggle));
    CHECK(!st.multi_permit);
    CHECK(DecodeHardKeyEvent(st, true, 1, 5, 1, &code, &val, &toggle));
    CHECK_EQ(code, 5);
    CHECK(!toggle);
    CHECK(!st.multi_permit);
    CHECK_EQ(st.active_code, 1);
    // Releasing the first key unlocks.
    CHECK(DecodeHardKeyEvent(st, true, 1, 1, 0, &code, &val, &toggle));
    CHECK(toggle);
    CHECK(st.multi_permit);
}

static void test_hardkey_permit_off_drops_but_tracks_tune_ok() {
    KeyFilterState st = {0, true, false};
    uint8_t code = 0, val = 0;
    bool toggle = false;
    CHECK(!DecodeHardKeyEvent(st, false, 1, 6, 1, &code, &val, &toggle));
    CHECK(!toggle);
    CHECK(st.multi_permit);
    CHECK_EQ(st.active_code, 0);
    CHECK(st.tune_ok_down);
    // Autorepeat (value 2) never changes the TUNE_OK flag.
    CHECK(!DecodeHardKeyEvent(st, false, 1, 6, 2, &code, &val, &toggle));
    CHECK(st.tune_ok_down);
}

static void test_hardkey_remap() {
    // 0xaa71c: platform 1 with region 1 or 2 swaps SETUP/PHONE/VOICE.
    CHECK_EQ(RemapHardKeyCode(4, 1, 1), 10);
    CHECK_EQ(RemapHardKeyCode(5, 1, 2), 4);
    CHECK_EQ(RemapHardKeyCode(3, 1, 1), 5);
    CHECK_EQ(RemapHardKeyCode(1, 1, 1), 1);
    CHECK_EQ(RemapHardKeyCode(4, 2, 1), 4);
    CHECK_EQ(RemapHardKeyCode(4, 1, 0), 4);
    CHECK_EQ(RemapHardKeyCode(4, 1, 3), 4);
}

static void test_knob_word_decoding() {
    uint8_t key = 0;
    CHECK(DecodeKnobWord(0x32, &key));
    CHECK_EQ(key, 7);  // TUNE_UP
    CHECK(DecodeKnobWord(0x33, &key));
    CHECK_EQ(key, 8);  // TUNE_DOWN
    CHECK(!DecodeKnobWord(0x00, &key));
    CHECK(!DecodeKnobWord(0x34, &key));
    CHECK(!DecodeKnobWord(-1, &key));
}

static void test_knob_fast_rotate() {
    // 0xaa304: six same-direction detents within 75 ms of the burst start
    // add 6 * 7 detents, each later one 7 more.
    KnobRotateState st = {2, 0};
    bool restart = false;
    CHECK_EQ(CalculateFastRotateNum(st, 0, 100000, &restart), 0u);
    CHECK(restart);
    CHECK_EQ(st.count, 1u);
    for (int i = 2; i <= 5; ++i) {
        CHECK_EQ(CalculateFastRotateNum(st, 0, 10 * i, &restart), 0u);
        CHECK(!restart);
    }
    CHECK_EQ(CalculateFastRotateNum(st, 0, 75, &restart), 42u);
    CHECK_EQ(CalculateFastRotateNum(st, 0, 75, &restart), 7u);
    // Burst older than 75 ms restarts the count.
    CHECK_EQ(CalculateFastRotateNum(st, 0, 76, &restart), 0u);
    CHECK(restart);
    CHECK_EQ(st.count, 1u);
    // A direction change restarts it too.
    CHECK_EQ(CalculateFastRotateNum(st, 1, 1, &restart), 0u);
    CHECK(restart);
    CHECK_EQ(st.direction, 1);
    CHECK_EQ(KnobDirectionKey(0), 7);
    CHECK_EQ(KnobDirectionKey(1), 8);
    CHECK_EQ(KnobDirectionKey(2), 0);
}

static void test_partial_reads_rejected() {
    int fds[2];
    CHECK(pipe(fds) == 0);
    int32_t word = 0;
    const unsigned char two[2] = {0x32, 0};
    CHECK(write(fds[1], two, 2) == 2);
    CHECK_EQ(ReadKnobWord(fds[0], &word), kReadShort);

    const int32_t full = 0x33;
    CHECK(write(fds[1], &full, 4) == 4);
    CHECK_EQ(ReadKnobWord(fds[0], &word), kReadOk);
    CHECK_EQ(word, 0x33);

    InputEvent32 ev;
    memset(&ev, 0, sizeof(ev));
    CHECK(write(fds[1], two, 2) == 2);
    CHECK_EQ(ReadInputEvent(fds[0], &ev), kReadShort);

    InputEvent32 in;
    memset(&in, 0, sizeof(in));
    in.type = 1;
    in.code = 10;
    in.value = 1;
    CHECK(write(fds[1], &in, sizeof(in)) == (ssize_t)sizeof(in));
    CHECK_EQ(ReadInputEvent(fds[0], &ev), kReadOk);
    CHECK_EQ(ev.code, 10);
    CHECK_EQ(ev.value, 1);

    close(fds[1]);
    CHECK_EQ(ReadKnobWord(fds[0], &word), kReadEof);
    close(fds[0]);
    CHECK_EQ(ReadKnobWord(fds[0], &word), kReadError);
}

static void test_multitouch_press_move_release_cycle() {
    MultiTouchTracker tr;
    memset(&tr, 0, sizeof(tr));
    VMULTITOUCH_DATA out;

    ts_sample s;
    memset(&s, 0, sizeof(s));
    s.x = 400; s.y = 438; s.pressure = 50;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.gesture, 0);
    CHECK_EQ(out.count, 1u);
    CHECK_EQ(out.x[0], 400);
    CHECK_EQ(out.y[0], 438);
    CHECK_EQ(out.state[0], 1);  // press

    s.x = 405; s.y = 440; s.pressure = 50;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.state[0], 3);  // move
    CHECK_EQ(out.x[0], 405);

    s.pressure = 0;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.state[0], 2);  // release

    // STOCK-QUIRK: 0x23b14 also emits a zero-pressure sample while idle
    // (state 0); VDisplayTask::OnTouchEvent (0x65308) ignores state 0.
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.state[0], 0);

    // No samples: nothing to emit.
    CHECK(!UpdateMultiTouch(tr, &s, 0, &out));
    CHECK(!UpdateMultiTouch(tr, 0, 1, &out));
}

static void test_multitouch_move_throttle() {
    MultiTouchTracker tr;
    memset(&tr, 0, sizeof(tr));
    tr.move_interval_ms = 20;
    VMULTITOUCH_DATA out;
    ts_sample s;
    memset(&s, 0, sizeof(s));
    s.pressure = 1;
    tr.now_ms = 1000;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));  // press
    tr.now_ms = 1001;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));  // first move starts the timer
    CHECK_EQ(out.state[0], 3);
    tr.now_ms = 1010;
    s.x = 7;
    CHECK(!UpdateMultiTouch(tr, &s, 1, &out));  // throttled, position kept
    CHECK_EQ(tr.x[0], 7);
    tr.now_ms = 1021;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.x[0], 7);
    // Release is never throttled.
    tr.now_ms = 1022;
    s.pressure = 0;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.state[0], 2);
}

static void test_multitouch_release_pending_flush() {
    // 0x23cac: with the release flag set, wait until every finger is up, then
    // report pressed / moving slots as released once.
    MultiTouchTracker tr;
    memset(&tr, 0, sizeof(tr));
    VMULTITOUCH_DATA out;
    ts_sample s[2];
    memset(s, 0, sizeof(s));
    s[0].pressure = 5;
    s[1].pressure = 5;
    CHECK(UpdateMultiTouch(tr, s, 2, &out));
    CHECK_EQ(out.count, 2u);
    tr.release_pending = true;
    CHECK(!FlushMultiTouchRelease(tr, s, 2, &out));
    CHECK(tr.release_pending);
    s[0].pressure = 0;
    s[1].pressure = 0;
    CHECK(FlushMultiTouchRelease(tr, s, 2, &out));
    CHECK(!tr.release_pending);
    CHECK_EQ(out.state[0], 2);
    CHECK_EQ(out.state[1], 2);
    CHECK_EQ(tr.state[0], 0);
}

static void test_multitouch_clamps_count() {
    MultiTouchTracker tr;
    memset(&tr, 0, sizeof(tr));
    VMULTITOUCH_DATA out;
    ts_sample s[4];
    memset(s, 0, sizeof(s));
    // DEVIATION: ts_read is asked for 3 samples; more is clamped.
    CHECK(UpdateMultiTouch(tr, s, 4, &out));
    CHECK_EQ(out.count, 3u);
}

int main() {
    RUN_TEST(test_ts_sample_size_is_24);
    RUN_TEST(test_input_event_is_16_bytes);
    RUN_TEST(test_hardkey_decoding_and_bounds);
    RUN_TEST(test_hardkey_radio_phone_pass_while_locked);
    RUN_TEST(test_hardkey_permit_off_drops_but_tracks_tune_ok);
    RUN_TEST(test_hardkey_remap);
    RUN_TEST(test_knob_word_decoding);
    RUN_TEST(test_knob_fast_rotate);
    RUN_TEST(test_partial_reads_rejected);
    RUN_TEST(test_multitouch_press_move_release_cycle);
    RUN_TEST(test_multitouch_move_throttle);
    RUN_TEST(test_multitouch_release_pending_flush);
    RUN_TEST(test_multitouch_clamps_count);
    return test_summary();
}
