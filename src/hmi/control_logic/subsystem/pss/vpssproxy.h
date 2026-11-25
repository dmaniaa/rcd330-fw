// Stock source: control_logic/subsystem/pss/vpssproxy.cpp
//
// Persistent settings (opt/pss).
// Phase 2 handles only the system setup block (0x4b) read reply, which
// decides region and language; the other blocks are TODO(phase3).
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PSS_VPSSPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PSS_VPSSPROXY_H

#include <stdint.h>

#include "control_logic/subsystem/proxy_decoders.h"
#include "service/utility/vbaseproxy.h"

class QThread;
class VPssRetryTimer;

class VPSSProxy : public VBaseProxy {
public:
    static VPSSProxy* Instance();

    void Init();
    // Ours (DEVIATION): creates the setup-read retry timer on `thread`, which
    // must run an event loop (VControlTask's). Without it no retry happens.
    void SetSetupReadRetryThread(QThread* thread);
    // Requests PSS block 0x4b (HMI_SYSTEM_SETUP_PSS_NEWBLK). A fresh request
    // (VMF thread) also restarts the status-10 retry budget.
    void OnSendPSSReadSystemSetupStatusRequest();
    // Ours (DEVIATION): the timed re-send of the 0x4b read (VPssRetryTimer,
    // VControlTask thread); keeps the retry budget.
    void ResendSystemSetupStatusRequest();

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VPSSProxy();

    bool m_logged;
    void SendSystemSetupReadMsg();

    // Ours: DEVIATION setup-read retry (see VPssRetryTimer).
    VPssRetryTimer* m_retry;
    SetupReadRetryState m_setup_retry;  // VMF thread only
    bool m_logged_no_retry_timer;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PSS_VPSSPROXY_H
