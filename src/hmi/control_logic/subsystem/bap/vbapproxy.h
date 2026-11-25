// Stock source: control_logic/subsystem/bap/vbapproxy.cpp (printlog in
// 0x000b61ac).
//
// BAP / CAN vehicle data via the VIP. Phase 2 keeps the boot sensor queries
// and the environment status (outside temperature, night mode) the status
// bar shows.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_BAP_VBAPPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_BAP_VBAPPROXY_H

#include <stdint.h>

#include "control_logic/subsystem/proxy_decoders.h"
#include "service/utility/vbaseproxy.h"

class VBAPProxy : public VBaseProxy {
public:
    static VBAPProxy* Instance();

    void Init();
    // Sends a BAP info request for one sensor id (0x3c..0x40, 0x58..0x5c);
    // other ids are ignored.
    void OnSetBAPInfo(uint8_t id);
    // Boot query: requests the ten sensor ids main() asks for.
    void RequestSensorInfo();

    const BapExtTemp& GetEnvStatus() const { return m_env; }
    // Vehicle country from 0xa2:0x06; 0 while not valid.
    uint8_t GetVehicleCountry() const;

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VBAPProxy();

    BapExtTemp m_env;      // +0x23c raw temp, +0x23d unit, +0x955 night
    uint8_t m_env_p0;      // +0x236
    uint8_t m_env_p6;      // +0x954
    uint8_t m_country_valid;  // +0x245
    uint8_t m_country;        // +0x246
    bool m_logged;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_BAP_VBAPPROXY_H
