// File name is ours; stock name unknown.
//
// Portable (no Qt) parts of the input drivers, host-tested by
// tests/test_input_drivers.cpp:
// - hard keys: VKeyDriver::readKeyDriver (0x000aa77c) and its code remap
//   (0x000aa71c);
// - knob: VKnobDriver::readKnobDriver (0x000aa3e0) and its fast-rotate
//   counter (0x000aa304 / 0x000aa2b8);
// - touch: VMultiTouch slot tracking (0x00023b14, 0x00023cac, 0x00023d34).
#ifndef RCD330_HMI_CONTROL_LOGIC_INPUT_INPUT_DECODERS_H
#define RCD330_HMI_CONTROL_LOGIC_INPUT_INPUT_DECODERS_H

#include <stdint.h>

#include "service/tslib/tslib_api.h"

// ---- device reads -------------------------------------------------------

// ARMv7 Linux struct input_event (16 bytes; 32-bit struct timeval).
struct InputEvent32 {
    int32_t tv_sec;
    int32_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};
typedef char input_event_must_be_16_bytes[sizeof(InputEvent32) == 16 ? 1 : -1];

enum ReadResult {
    kReadOk,     // the whole record
    kReadShort,  // 0 < n < record size; the record is dropped
    kReadEof,    // read returned 0
    kReadError   // read returned -1 (errno kept); EINTR is retried
};

// One 16-byte record from /dev/event0.
ReadResult ReadInputEvent(int fd, InputEvent32* ev);
// One 4-byte word from /dev/tuner_knob.
ReadResult ReadKnobWord(int fd, int32_t* word);

// ---- hard keys ----------------------------------------------------------

const uint16_t kEvKey = 1;
const uint16_t kHardKeyMax = 10;  // 1 RADIO .. 10 APP
const uint8_t kHardKeyTuneOk = 6;

// VKeyDriver state: active_code is +0x10 (keycode_bak), tune_ok_down +0x18.
// multi_permit mirrors hmi_wrapper_igdt_get_mutihardkey_permit(); the caller
// loads it before decoding and writes it back when *out_lock_toggle is set.
struct KeyFilterState {
    uint16_t active_code;
    bool multi_permit;
    bool tune_ok_down;
};

// 0xaa71c: platform 1 with region 1 or 2 maps SETUP->APP, PHONE->SETUP,
// VOICE->PHONE; every other code is unchanged.
uint16_t RemapHardKeyCode(uint16_t code, int platform_type, int region_mode);

// 0xaa77c after the read. Returns true when (out_code, out_val) must go to
// VControlTask::KeyDriverNotifySlot. *out_lock_toggle is true when the
// multi-hardkey and touch permits must both be set to st.multi_permit.
// code is the already remapped key code (RemapHardKeyCode keeps codes > 10
// out of range, so remapping first does not change the bounds check).
bool DecodeHardKeyEvent(KeyFilterState& st, bool hardkey_permit, uint16_t type, uint16_t code, int32_t value,
                        uint8_t* out_code, uint8_t* out_val, bool* out_lock_toggle);

// ---- knob ---------------------------------------------------------------

const int32_t kKnobWordUp = 0x32;
const int32_t kKnobWordDown = 0x33;
const uint8_t kHardKeyTuneUp = 7;
const uint8_t kHardKeyTuneDown = 8;

// 0xaa3e0: 0x32 -> 7 (TUNE_UP), 0x33 -> 8 (TUNE_DOWN); other words dropped.
bool DecodeKnobWord(int32_t word, uint8_t* out_key_code);

// VKnobDriver +0x1c (last direction, 0 up / 1 down, initially 2) and +0x18
// (detents in the current burst).
struct KnobRotateState {
    uint8_t direction;
    uint32_t count;
};

// 0xaa304 without its screen 0xfe gate. elapsed_ms is the burst timer
// (QTime +0x20); *restart_timer tells the caller to restart it. Returns the
// number of extra detents to post (0, 6 * 7 on the sixth detent of a burst
// within 75 ms, then 7 each).
uint32_t CalculateFastRotateNum(KnobRotateState& st, uint8_t direction, int elapsed_ms, bool* restart_timer);
// 0xaa2b8: direction -> posted key (0 -> 7, 1 -> 8, else 0).
uint8_t KnobDirectionKey(uint8_t direction);

// ---- touch --------------------------------------------------------------

const int kMultiTouchSlots = 3;

// SigTouchEvent payload (0x00023d34, VDisplayTask::OnTouchEvent 0x00065308):
// state 0 idle, 1 press, 2 release, 3 move. OnTouchEvent ignores gesture != 0.
struct VMULTITOUCH_DATA {
    int32_t gesture;
    uint32_t count;
    int32_t x[kMultiTouchSlots];
    int32_t y[kMultiTouchSlots];
    int32_t state[kMultiTouchSlots];
};

// VMultiTouch slots (+0x24, stride 0x14: x, y, pressure, state, id) plus
// count (+0x14), the move throttle (+0x18, ms, 0 = off) and the release
// flag (+0x11). The move QTime (+0x1c) is now_ms / move_start_ms: the caller
// stores a monotonic millisecond clock in now_ms before each update.
struct MultiTouchTracker {
    int32_t x[kMultiTouchSlots];
    int32_t y[kMultiTouchSlots];
    int32_t state[kMultiTouchSlots];
    uint32_t pressure[kMultiTouchSlots];
    uint8_t id[kMultiTouchSlots];
    uint32_t count;
    int32_t move_interval_ms;
    int32_t now_ms;
    int32_t move_start_ms;
    bool release_pending;
};

// 0x23b14 then, when it does not throttle, 0x23d34. Returns true when *out
// must be emitted.
bool UpdateMultiTouch(MultiTouchTracker& tr, const ts_sample* samples, int n, VMULTITOUCH_DATA* out);
// 0x23cac then, when every finger is up, 0x23d34; clears release_pending.
bool FlushMultiTouchRelease(MultiTouchTracker& tr, const ts_sample* samples, int n, VMULTITOUCH_DATA* out);
// 0x23d34: snapshot; released slots go back to idle.
void BuildMultiTouchData(MultiTouchTracker& tr, VMULTITOUCH_DATA* out);

#endif  // RCD330_HMI_CONTROL_LOGIC_INPUT_INPUT_DECODERS_H
