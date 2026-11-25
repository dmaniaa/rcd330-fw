// Stock source: control_logic/subsystem/appconnect/vappconnectproxy.cpp
// (printlog in 0x0034ac5c).
//
// App-Connect coding (region and available smartphone protocols) read from
// PSS, plus connect_mgr notifications (TODO(phase3)).
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_APPCONNECT_VAPPCONNECTPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_APPCONNECT_VAPPCONNECTPROXY_H

#include <stdint.h>

#include "control_logic/subsystem/proxy_decoders.h"
#include "service/utility/vbaseproxy.h"

class VAppConnectProxy : public VBaseProxy {
public:
    static VAppConnectProxy* Instance();

    void Init();
    // Asks opt/pss for the App-Connect coding; the reply is 0x72:0x04.
    void QueryPssCoding();
    // True when any protocol is available for the current region.
    bool IsCoded() const;
    // 1 until PSS answers (stock ctor value); 2 = ROW / FM region.
    int GetRegionMode() const;
    int GetSubMode() const;
    // Region 1 flags: CarLife, CarPlay, MirrorLink.
    void GetAvailableApps(uint8_t* cl, uint8_t* cp, uint8_t* ml) const;
    // Region 2 flags (stock name): Android Auto, CarPlay, MirrorLink.
    void GetFMAvailableAppRow(uint8_t* aa, uint8_t* cp, uint8_t* ml) const;

    // True when a phone of a protocol the current region offers is
    // connected (VControlTask maps page 8 to 9 when it is not).
    bool IsAppDeviceConnected() const;

    const AppConnectCoding& GetCoding() const { return m_coding; }

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VAppConnectProxy();

    AppConnectCoding m_coding;  // +0x24 region, +0x28 sub, +0x2e..0x34 flags
    int m_device_type;          // +0x1c: 2 iOS, 3 Android (connect_mgr, phase 3)
    bool m_logged_phase3;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_APPCONNECT_VAPPCONNECTPROXY_H
