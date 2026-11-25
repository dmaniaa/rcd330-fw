// Stock source: control_logic/subsystem/amfm/vamfmproxy.cpp
//
// AM/FM tuner on the VIP (via opt/vgw). Phase 2 keeps the three boot
// queries main() sends.
// RecieveVmfMsg is TODO(phase3): it logs the first message and drops all.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_AMFM_VAMFMPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_AMFM_VAMFMPROXY_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

class VAMFMProxy : public VBaseProxy {
public:
    static VAMFMProxy* Instance();

    void Init();
    void OnGetCurrentInfoOpera();
    // Name is ours: second boot query (0x8f:0x22, item 0x3b).
    void OnGetTunerListOpera();
    void OnGetSeekmodeOpera();

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VAMFMProxy();

    bool m_logged;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_AMFM_VAMFMPROXY_H
