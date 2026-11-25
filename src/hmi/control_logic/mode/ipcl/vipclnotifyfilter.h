// Stock source: control_logic/mode/ipcl/vipclnotifyfilter.cpp
//
// Faceplate hard keys. VControlTask posts every driver key as
// {notify, 3, {4, 1, key, 1 press | 2 release}}; HandleLPCLHardKey feeds it
// to VHMIKey, whose callbacks report (key, 3 press | 4 short release |
// 5 long press | 6 long release | ...) through NotifyDisplayKey
// (SigKeyEventNotify). VDisplayTask offers that to the IGDT page first and,
// if no page takes it, posts it back as {notify, 4, {4, 1, key, state}};
// only then do the per-key handlers (RADIO, APP, ...) run.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_IPCL_VIPCLNOTIFYFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_IPCL_VIPCLNOTIFYFILTER_H

#include "control_logic/mode/ipcl/vhmikey.h"
#include "control_logic/mode/vbasefilter.h"

class VIPCLNotifyFilter : public VBaseFilter {
public:
    enum { kKeyCodeCount = 16 };

    explicit VIPCLNotifyFilter(IFilterActions* actions);
    virtual ~VIPCLNotifyFilter();

    // VControlTask::HMIKeyTimeOutSlot (0x00031ed8) ticks the key states.
    void KeyTimeout(uint32_t elapsed_ms);
    VHMIKey& Keys() { return m_keys; }

protected:
    virtual FILTER_MSG HandleLPCLHardKey(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyMedia(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyRadio(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyPhone(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyVoice(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeySet(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyTune(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyTuneUp(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyTuneDown(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyApp(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyEject(const FILTER_MSG& msg);

private:
    // Review focus 5: a phase-3 key logs once and is passed on unchanged.
    FILTER_MSG Phase3Key(const FILTER_MSG& msg, const char* name);

    VHMIKey m_keys;                     // +0x10 (heap object in stock)
    int m_radio_last_state;             // stock global 0x009704f4
    bool m_logged_key[kKeyCodeCount];   // ours: phase-3 log-once per key
    bool m_logged_unknown_key;          // ours
    bool m_logged_phase3_radio;         // ours
    bool m_logged_phase3_app;           // ours
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_IPCL_VIPCLNOTIFYFILTER_H
