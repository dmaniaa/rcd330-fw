// File name is ours; stock name unknown (base vtable 0x003eddb0).
//
// Notify/command filter chain. VControlTask feeds subsystem notifications
// (power, keys, ...) into the first filter of a chain; every filter returns a
// possibly rewritten FILTER_MSG which, while it stays valid, is passed to
// each of its next filters (DispatchMsg, 0x0003962c). Notify filters usually
// turn a notification into a command (valid 1) for the command filters
// behind VPrehandleFilter.
//
// Stock dispatch is an 84-slot vtable: HandleMsg -> HandleNotifyMsg (by
// para[0] subsystem) -> per-subsystem dispatcher (by para[1]) -> handler.
// Only the power (para[0] 2) and IPCL key (para[0] 4) paths exist in phase
// 2; every other subsystem passes through unchanged like the stock base
// slots do.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_VBASEFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_VBASEFILTER_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

class IFilterActions;

enum {
    // Stock FILTER_MSG is 0x100c bytes (valid word, type word, 0x1004 para
    // bytes; QMetaType ctor 0x00036860). No phase-2 message uses more than
    // a few para bytes; producers must not write past kFilterParaSize.
    kFilterParaSize = 64
};

enum {
    kFilterInvalid = 0,  // consumed: forwarded nowhere
    kFilterCmd = 1,      // command, handled by HandleCmdMsg
    kFilterNotify = 2    // notification, handled by HandleNotifyMsg
};

// para[0] subsystem ids used in phase 2.
enum {
    kFilterSubPower = 0x02,
    kFilterSubIpcl = 0x04
};

struct FILTER_MSG {
    uint8_t valid;   // kFilterInvalid / kFilterCmd / kFilterNotify
    uint32_t type;   // 0 = invalid; 2 command, 3 driver/VMF notify, 4 re-posted key
    uint8_t para[kFilterParaSize];

    void Reset();
};

// Logs through log_simple/printlog on target, stderr in host tests.
void FilterLog(const char* file, const char* func, int line, const char* fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 4, 5)))
#endif
    ;

class VBaseFilter {
public:
    // actions may be NULL for filters that never act (tests, pass-through).
    explicit VBaseFilter(IFilterActions* actions);
    virtual ~VBaseFilter();

    // False for NULL or a filter already in the list.
    bool AddNextFilter(VBaseFilter* next);
    size_t NextCount() const { return m_next.size(); }
    // Runs HandleMsg and forwards a valid result to every next filter.
    void DispatchMsg(const FILTER_MSG& msg);

    virtual FILTER_MSG HandleMsg(const FILTER_MSG& msg);

protected:
    virtual FILTER_MSG HandleCmdMsg(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleNotifyMsg(const FILTER_MSG& msg);

    // para[0] == 2 (power): dispatched by para[1].
    virtual FILTER_MSG HandlePowerNotify(const FILTER_MSG& msg);
    virtual FILTER_MSG HandlePowerVOLStatus(const FILTER_MSG& msg);    // para[1] 2
    virtual FILTER_MSG HandlePowerAuxStatus(const FILTER_MSG& msg);    // para[1] 3
    virtual FILTER_MSG HandlePowerEjectStatus(const FILTER_MSG& msg);  // para[1] 4
    virtual FILTER_MSG HandleAudioModeStatus(const FILTER_MSG& msg);   // para[1] 5

    // para[0] == 4 (IPCL hard keys): para[1] 1, para[2] key, para[3] state.
    virtual FILTER_MSG HandleIPCLNotify(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKey(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleLPCLHardKeyMedia(const FILTER_MSG& msg);     // key 2
    virtual FILTER_MSG HandleLPCLHardKeyRadio(const FILTER_MSG& msg);     // key 1
    virtual FILTER_MSG HandleLPCLHardKeyPhone(const FILTER_MSG& msg);     // key 5
    virtual FILTER_MSG HandleLPCLHardKeyVoice(const FILTER_MSG& msg);     // key 3
    virtual FILTER_MSG HandleLPCLHardKeySet(const FILTER_MSG& msg);       // key 4
    virtual FILTER_MSG HandleLPCLHardKeyTune(const FILTER_MSG& msg);      // key 6
    virtual FILTER_MSG HandleLPCLHardKeyTuneUp(const FILTER_MSG& msg);    // key 7
    virtual FILTER_MSG HandleLPCLHardKeyTuneDown(const FILTER_MSG& msg);  // key 8
    virtual FILTER_MSG HandleLPCLHardKeyApp(const FILTER_MSG& msg);       // key 10
    virtual FILTER_MSG HandleLPCLHardKeyEject(const FILTER_MSG& msg);     // key 9

    IFilterActions* m_actions;

private:
    std::vector<VBaseFilter*> m_next;  // +0x04..+0x0c

    VBaseFilter(const VBaseFilter&);
    VBaseFilter& operator=(const VBaseFilter&);
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_VBASEFILTER_H
