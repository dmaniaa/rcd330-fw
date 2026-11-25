// File name is ours; stock name unknown.
#include "control_logic/mode/ipcl/vhmikey.h"

#include <string.h>

namespace {

const uint32_t kDefaultLongPressMs = 2000;
// Extend-long-held keys need 3000 ms more than the long-press time (0x0003c564).
const uint32_t kUltralongExtraMs = 3000;

}  // namespace

// 0x0003bc10 VHMIKey::VHMIKey
VHMIKey::VHMIKey() : m_long_press_ms(kDefaultLongPressMs), m_ctx(0) {
    memset(&m_cbs, 0, sizeof(m_cbs));
}

// 0x0003c9bc VHMIKey::AddKey
bool VHMIKey::AddKey(int key) {
    if (key == 0 || Find(key) != 0) {
        return false;
    }
    Entry e = {key, 0, 0};
    m_keys.push_back(e);
    return true;
}

// 0x0003bd90 VHMIKey::SetLongPressTime
void VHMIKey::SetLongPressTime(uint32_t ms) {
    m_long_press_ms = ms;
}

void VHMIKey::SetCallbacks(const VHMIKeyCallbacks& cbs, void* ctx) {
    m_cbs = cbs;
    m_ctx = ctx;
}

VHMIKey::Entry* VHMIKey::Find(int key) {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i].key == key) {
            return &m_keys[i];
        }
    }
    return 0;
}

int VHMIKey::StateOf(int key) const {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i].key == key) {
            return m_keys[i].state;
        }
    }
    return -1;
}

// 0x0003c820 VHMIKey::Press
// Only an idle key starts a press; a repeat while held changes nothing.
void VHMIKey::Press(int key) {
    Entry* e = Find(key);
    if (e != 0 && e->state == 0) {
        e->state = 1;
        e->counter = 0;
    }
    ProcessStates();
}

// 0x0003c750 VHMIKey::Release
void VHMIKey::Release(int key) {
    Entry* e = Find(key);
    if (e != 0) {
        switch (e->state) {
        case 2:
            e->state = 3;
            e->counter = 0;
            break;
        case 5:
            e->state = 7;
            e->counter = 0;
            break;
        case 8:
            e->state = 9;
            e->counter = 0;
            break;
        case 11:
            e->state = 12;
            e->counter = 0;
            break;
        default:
            break;
        }
    }
    ProcessStates();
}

// 0x0003c564 VHMIKey::Tick
// TODO(phase3): at exactly 1000 ms held, stock calls the VServiceProxy
// key hook 0x00201a08(key) (service-mode key sequence).
void VHMIKey::Tick(uint32_t elapsed_ms) {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        Entry& e = m_keys[i];
        if (e.state == 2) {
            e.counter += elapsed_ms;
            if (e.counter >= m_long_press_ms) {
                e.state = 4;
                e.counter = 0;
            }
        } else if (e.state == 5) {
            e.counter += elapsed_ms;
            if (e.counter >= m_long_press_ms) {
                e.state = 6;
                e.counter = 0;
            }
        } else if (e.state == 8) {
            e.counter += elapsed_ms;
            if (e.counter >= m_long_press_ms + kUltralongExtraMs) {
                e.state = 10;
                e.counter = 0;
            }
        }
    }
    ProcessStates();
}

// 0x0003bd98 VHMIKey::ResetAll
void VHMIKey::ResetAll() {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        m_keys[i].state = 0;
        m_keys[i].counter = 0;
    }
}

void VHMIKey::Fire(VHMIKeyCallback cb, const Entry& e, int old_state) {
    if (cb != 0) {
        cb(m_ctx, e.key, old_state);
    }
}

// Name is ours: stock has no such function; Press, Release and Tick all
// jump to one shared tail (label at 0x0003bebc) that does this.
// The state is advanced before its callback runs, as in stock.
// Index loop: a callback may not add keys, but stays safe if it did.
void VHMIKey::ProcessStates() {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        const int old_state = m_keys[i].state;
        VHMIKeyCallback cb = 0;
        int next = old_state;
        switch (old_state) {
        case 1:
            cb = m_cbs.press;
            next = 2;
            break;
        case 3:
            cb = m_cbs.short_release;
            next = 0;
            break;
        case 4:
            cb = m_cbs.long_press;
            next = 5;
            break;
        case 6:
            cb = m_cbs.extend_long_press;
            next = 8;
            break;
        case 7:
            cb = m_cbs.long_release;
            next = 0;
            break;
        case 9:
            cb = m_cbs.extend_long_release;
            next = 0;
            break;
        case 10:
            cb = m_cbs.ultralong_press;
            next = 11;
            break;
        case 12:
            cb = m_cbs.ultralong_release;
            next = 0;
            break;
        default:
            continue;
        }
        m_keys[i].state = next;
        m_keys[i].counter = 0;
        const Entry e = m_keys[i];
        Fire(cb, e, old_state);
    }
}
