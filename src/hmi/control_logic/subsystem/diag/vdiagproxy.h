// Stock source: control_logic/subsystem/diag/vdiagproxy.cpp
//
// Diagnostics (opt/diag, opt/diag_eol). Init announces the HMI to diag.
// RecieveVmfMsg is TODO(phase3): it logs the first message and drops all.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_DIAG_VDIAGPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_DIAG_VDIAGPROXY_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

class VDiagProxy : public VBaseProxy {
public:
    static VDiagProxy* Instance();

    void Init();
    // Sends a diag response; NULL or more than 0x32 bytes is dropped.
    void OnResponesToDIAG(const uint8_t* buf, uint32_t len);
    // Name is ours: the 0xa0:0x07 message Init sends.
    void NotifyDiagReady();

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VDiagProxy();

    bool m_logged;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_DIAG_VDIAGPROXY_H
