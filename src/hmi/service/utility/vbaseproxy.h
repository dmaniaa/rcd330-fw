// Stock source: ../service/utility/vbaseproxy.cpp (printlog in 0x0005fe68).
//
// Base of the subsystem proxies: subscribes VMF groups through VmfThread,
// frames and sends raw [group, sub, len_hi, len_lo, payload...] buffers, and
// hands received basic packets to RecieveVmfMsg.
#ifndef RCD330_HMI_SERVICE_UTILITY_VBASEPROXY_H
#define RCD330_HMI_SERVICE_UTILITY_VBASEPROXY_H

#include <stdint.h>

#include "service/vmf/vmf_frame.h"

class VBaseProxy : public IVmfListener {
public:
    VBaseProxy();
    virtual ~VBaseProxy();

    // Registers this proxy for each of the count groups, then subscribes them.
    void RegisterVmfGroup(const uint8_t* groups, uint32_t count);
    // VmfPackMsg plus the stock warning for a short buffer.
    bool PackMsg(uint8_t out[VMF_PKT_SIZE], const uint8_t* buf, uint32_t len);
    // Frames buf and sends it; a malformed buf is logged and not sent.
    // False when nothing left (malformed buf, VMF not connected yet, send
    // error).
    bool SendVmfMsg(const uint8_t* buf, uint32_t len);

    virtual void OnVmfPacket(const uint8_t* pkt);
    // Stock spelling. Not pure: stock vtable slot 3 (0x0005fd20) is an empty
    // body.
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
};

#endif // RCD330_HMI_SERVICE_UTILITY_VBASEPROXY_H
