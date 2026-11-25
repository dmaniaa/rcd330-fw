// File name is ours; stock name unknown.
// Stock class: typeinfo "7VHMIKey", vtable 0x003edf28.
//
// Per-key press / long-press state machine owned by VIPCLNotifyFilter.
// Press and Release move a key's state; Tick adds elapsed milliseconds while
// a key is held; after each of them ProcessStates fires the callback for
// every key whose state calls for one and moves it on:
//
//   1 pressed            -> press             -> 2 held
//   2 held, >= long ms   -> 4                 (Tick)
//   3 released from 2    -> short release     -> 0
//   4                    -> long press        -> 5 long held
//   5 long held, >= long -> 6                 (Tick)
//   6                    -> extend long press -> 8 extend held
//   7 released from 5    -> long release      -> 0
//   8 extend held, >= long + 3000 -> 10       (Tick)
//   9 released from 8    -> extend release    -> 0
//  10                    -> ultralong press   -> 11 ultralong held
//  12 released from 11   -> ultralong release -> 0
//
// Callbacks get (key, state before the transition). Stock also tracks key
// combinations (+0x10); VIPCLNotifyFilter registers none, so they are not
// ported.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_IPCL_VHMIKEY_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_IPCL_VHMIKEY_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

typedef void (*VHMIKeyCallback)(void* ctx, int key, int old_state);

// Stock offsets +0x20..+0x3c, set by eight one-line setters
// (0x0003be7c..0x0003beb4).
struct VHMIKeyCallbacks {
    VHMIKeyCallback press;                // +0x20
    VHMIKeyCallback short_release;        // +0x24
    VHMIKeyCallback long_press;           // +0x28
    VHMIKeyCallback long_release;         // +0x2c
    VHMIKeyCallback extend_long_press;    // +0x30
    VHMIKeyCallback extend_long_release;  // +0x34
    VHMIKeyCallback ultralong_press;      // +0x38
    VHMIKeyCallback ultralong_release;    // +0x3c
};

class VHMIKey {
public:
    VHMIKey();

    // False for key 0 or a key already registered.
    bool AddKey(int key);
    void SetLongPressTime(uint32_t ms);
    // DEVIATION: one setter instead of eight; ctx is passed to every
    // callback (stock callbacks are free functions using
    // VControlTask::Instance()).
    void SetCallbacks(const VHMIKeyCallbacks& cbs, void* ctx);

    void Press(int key);
    void Release(int key);
    void Tick(uint32_t elapsed_ms);
    // Back to idle without callbacks (multi-hardkey permit, 0x0003bd98).
    void ResetAll();

    int StateOf(int key) const;  // -1 if not registered (test helper, ours)

private:
    struct Entry {
        int key;
        int state;
        uint32_t counter;
    };

    Entry* Find(int key);
    void ProcessStates();
    void Fire(VHMIKeyCallback cb, const Entry& e, int old_state);

    std::vector<Entry> m_keys;  // +0x04..+0x0c, 12-byte entries
    uint32_t m_long_press_ms;   // +0x1c, 2000 by default
    VHMIKeyCallbacks m_cbs;
    void* m_ctx;
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_IPCL_VHMIKEY_H
