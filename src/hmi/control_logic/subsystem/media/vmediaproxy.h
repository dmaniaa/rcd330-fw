// Stock source: control_logic/subsystem/media/vmediaproxy.cpp
//
// opt/media proxy. Phase 2 keeps the boot device-state reply main() sends.
// RecieveVmfMsg is TODO(phase3): it logs the first message and drops all.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_MEDIA_VMEDIAPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_MEDIA_VMEDIAPROXY_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

class VMediaProxy : public VBaseProxy {
public:
    static VMediaProxy* Instance();

    void Init();
    // Stock name (printlog): sends MSG_MEDIA_APP_DEVICE_STATE_RET.
    void OnGetMsg(uint8_t state);

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VMediaProxy();

    bool m_logged;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_MEDIA_VMEDIAPROXY_H
