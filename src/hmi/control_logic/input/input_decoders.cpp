// File name is ours; stock name unknown.
#include "control_logic/input/input_decoders.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

namespace {

// Fast-rotate burst window (0xaa35c: elapsed > 75 restarts the burst).
const int kFastRotateWindowMs = 75;
const uint32_t kFastRotateFirst = 6;      // detent that triggers the first boost
const uint32_t kFastRotateRepeat = 7;     // DuplicateRotation multiplier

ReadResult ReadRecord(int fd, void* buf, size_t size) {
    ssize_t n;
    do {
        n = read(fd, buf, size);
    } while (n < 0 && errno == EINTR);  // DEVIATION: stock treats EINTR as an error.
    if (n < 0) {
        return kReadError;
    }
    if (n == 0) {
        return kReadEof;
    }
    return static_cast<size_t>(n) == size ? kReadOk : kReadShort;
}

bool IsRadioOrPhone(uint16_t code) {
    return code == 1 || code == 5;
}

}  // namespace

ReadResult ReadInputEvent(int fd, InputEvent32* ev) {
    return ReadRecord(fd, ev, sizeof(*ev));
}

ReadResult ReadKnobWord(int fd, int32_t* word) {
    return ReadRecord(fd, word, sizeof(*word));
}

// 0x000aa71c VKeyDriver::RemapKeyCode (name is ours)
uint16_t RemapHardKeyCode(uint16_t code, int platform_type, int region_mode) {
    if (platform_type != 1) {
        return code;
    }
    if (static_cast<unsigned>(region_mode - 1) > 1u) {
        return code;
    }
    if (code == 4) {
        return 10;
    }
    if (code == 5) {
        return 4;
    }
    if (code == 3) {
        return 5;
    }
    return code;
}

// 0x000aa77c VKeyDriver::readKeyDriver
// With the multi-hardkey permit set, a press takes the lock (permits off)
// and becomes the active key; while locked only the active key (any value)
// passes and its event releases the lock, except that RADIO and PHONE pass
// for each other. STOCK-QUIRK: code 0 passes the bounds check, and an
// autorepeat (value 2) of the active key releases the lock like a release.
bool DecodeHardKeyEvent(KeyFilterState& st, bool hardkey_permit, uint16_t type, uint16_t code, int32_t value,
                        uint8_t* out_code, uint8_t* out_val, bool* out_lock_toggle) {
    *out_lock_toggle = false;
    if (type != kEvKey || code > kHardKeyMax) {
        return false;
    }
    bool post = false;
    if (hardkey_permit) {
        if (!st.multi_permit) {
            if (code == st.active_code) {
                st.multi_permit = true;
                *out_lock_toggle = true;
                post = true;
            } else if (IsRadioOrPhone(st.active_code) && IsRadioOrPhone(code)) {
                post = true;
            }
        } else {
            if (value == 1) {
                st.multi_permit = false;
                *out_lock_toggle = true;
            }
            st.active_code = code;
            post = true;
        }
    }
    if (code == kHardKeyTuneOk && (value == 0 || value == 1)) {
        st.tune_ok_down = value == 1;
    }
    *out_code = static_cast<uint8_t>(code);
    *out_val = static_cast<uint8_t>(value);
    return post;
}

// 0x000aa3e0 VKnobDriver::readKnobDriver
bool DecodeKnobWord(int32_t word, uint8_t* out_key_code) {
    if (word == kKnobWordUp) {
        *out_key_code = kHardKeyTuneUp;
        return true;
    }
    if (word == kKnobWordDown) {
        *out_key_code = kHardKeyTuneDown;
        return true;
    }
    return false;
}

// 0x000aa304 VKnobDriver::CalculateFastRotateNum
// The burst timer restarts only on a direction change or a stale burst, so
// the 75 ms window counts from the burst's first detent.
uint32_t CalculateFastRotateNum(KnobRotateState& st, uint8_t direction, int elapsed_ms, bool* restart_timer) {
    *restart_timer = false;
    if (st.direction != direction) {
        st.count = 1;
        st.direction = direction;
        *restart_timer = true;
        return 0;
    }
    ++st.count;
    if (elapsed_ms > kFastRotateWindowMs) {
        st.count = 1;
        *restart_timer = true;
        return 0;
    }
    if (st.count == kFastRotateFirst) {
        return kFastRotateFirst * kFastRotateRepeat;  // DuplicateRotation(dir, count)
    }
    if (st.count < kFastRotateFirst) {
        return 0;
    }
    return kFastRotateRepeat;  // DuplicateRotation(dir, 1)
}

// 0x000aa2b8 VKnobDriver::DuplicateRotation
uint8_t KnobDirectionKey(uint8_t direction) {
    if (direction == 0) {
        return kHardKeyTuneUp;
    }
    if (direction == 1) {
        return kHardKeyTuneDown;
    }
    return 0;
}

// 0x00023d34 VMultiTouch::GetTouchData (name is ours)
void BuildMultiTouchData(MultiTouchTracker& tr, VMULTITOUCH_DATA* out) {
    // DEVIATION: stock leaves the slots past count uninitialised.
    memset(out, 0, sizeof(*out));
    out->gesture = 0;
    out->count = tr.count;
    for (uint32_t i = 0; i < tr.count && i < static_cast<uint32_t>(kMultiTouchSlots); ++i) {
        out->x[i] = tr.x[i];
        out->y[i] = tr.y[i];
        out->state[i] = tr.state[i];
        if (tr.state[i] == 2) {
            tr.state[i] = 0;
        }
    }
}

// 0x00023b14 VMultiTouch::UpdateTouchState (name is ours)
// Per sample slot: idle -> press on pressure; press / move -> move while
// pressed (move reports throttled to one per move_interval_ms), -> release
// when the pressure drops. The result is "skip" only when every sample was a
// throttled move. STOCK-QUIRK: a zero-pressure sample on an idle slot is
// reported (state 0).
bool UpdateMultiTouch(MultiTouchTracker& tr, const ts_sample* samples, int n, VMULTITOUCH_DATA* out) {
    // DEVIATION: stock reports NULL samples (never passed) as an event.
    if (samples == 0 || n <= 0) {
        return false;
    }
    // DEVIATION: stock writes past its three slots if ts_read returns more.
    if (n > kMultiTouchSlots) {
        n = kMultiTouchSlots;
    }
    bool skip = true;
    for (int i = 0; i < n; ++i) {
        const uint32_t pressure = samples[i].pressure;
        switch (tr.state[i]) {
        case 0:
            if (pressure != 0) {
                tr.state[i] = 1;
            }
            skip = false;
            break;
        case 1:
            if (pressure == 0) {
                tr.state[i] = 2;
            } else {
                tr.state[i] = 3;
                if (tr.move_interval_ms != 0) {
                    tr.move_start_ms = tr.now_ms;  // QTime::start
                }
            }
            skip = false;
            break;
        case 3:
            if (pressure == 0) {
                tr.state[i] = 2;
                skip = false;
            } else if (tr.move_interval_ms == 0) {
                skip = false;
            } else if (tr.now_ms - tr.move_start_ms >= tr.move_interval_ms) {
                tr.move_start_ms = tr.now_ms;
                skip = false;
            }
            break;
        default:
            skip = false;
            break;
        }
        tr.x[i] = samples[i].x;
        tr.y[i] = samples[i].y;
        tr.pressure[i] = pressure;
        tr.id[i] = samples[i].id;
    }
    tr.count = static_cast<uint32_t>(n);
    if (skip) {
        return false;
    }
    BuildMultiTouchData(tr, out);
    return true;
}

// 0x00023cac VMultiTouch::ReleaseAllTouch (name is ours)
bool FlushMultiTouchRelease(MultiTouchTracker& tr, const ts_sample* samples, int n, VMULTITOUCH_DATA* out) {
    if (samples == 0) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (samples[i].pressure != 0) {
            return false;
        }
    }
    for (uint32_t i = 0; i < tr.count && i < static_cast<uint32_t>(kMultiTouchSlots); ++i) {
        if (tr.state[i] == 1 || tr.state[i] == 3) {
            tr.state[i] = 2;
        }
    }
    tr.release_pending = false;
    BuildMultiTouchData(tr, out);
    return true;
}
