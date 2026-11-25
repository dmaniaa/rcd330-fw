// File name is ours; stock name unknown.
#include "service/vmf/vmf_frame.h"

#include <string.h>

#include <algorithm>

// 0x0005fe68 VBaseProxy::PackMsg
bool VmfPackMsg(uint8_t out_pkt[VMF_PKT_SIZE], const uint8_t* raw_buf, uint32_t raw_len) {
    memset(out_pkt, 0, VMF_PKT_SIZE);
    if (raw_buf == 0 || raw_len < 4) {
        return false;
    }
    const uint32_t payload_len = (static_cast<uint32_t>(raw_buf[2]) << 8) | raw_buf[3];
    if (payload_len > VMF_MAX_PAYLOAD || payload_len != raw_len - 4) {
        return false;
    }
    out_pkt[0] = 2;
    out_pkt[0x14] = raw_buf[0];
    out_pkt[0x15] = raw_buf[1];
    out_pkt[0x18] = static_cast<uint8_t>(payload_len & 0xff);
    out_pkt[0x19] = static_cast<uint8_t>(payload_len >> 8);
    if (payload_len != 0) {
        memcpy(out_pkt + VMF_HDR_SIZE, raw_buf + 4, payload_len);
    }
    return true;
}

// DEVIATION: stock VBaseProxy::OnVmfPacket (0x0005fd6c, outside the
// decompiled function set, so untagged) checks only pkt[0] == 2. The length
// bound adds a guard against a header that claims more than the receive
// buffer holds; libvmf_client's nw_vmf_receive already refuses such packets,
// so nothing stock accepts is dropped.
bool VmfUnpackMsg(const uint8_t pkt[VMF_PKT_SIZE], VmfUnpacked* out) {
    if (pkt == 0 || out == 0 || pkt[0] != 2) {
        return false;
    }
    const uint16_t len = static_cast<uint16_t>(pkt[0x18] | (pkt[0x19] << 8));
    if (len > VMF_RX_MAX_PAYLOAD) {
        return false;
    }
    out->group = pkt[0x14];
    out->sub = pkt[0x15];
    out->len = len;
    out->payload = pkt + VMF_HDR_SIZE;
    return true;
}

// 0x00060720 VmfThread::AddGroup
bool VmfRouter::AddGroup(uint8_t grp) {
    if (std::find(m_groups.begin(), m_groups.end(), grp) != m_groups.end()) {
        return false;
    }
    m_groups.push_back(grp);
    return true;
}

// 0x000604fc VmfThread::AddListener
void VmfRouter::AddListener(IVmfListener* listener, uint8_t grp) {
    if (listener == 0) {
        return;
    }
    m_listeners.push_back(std::make_pair(listener, grp));
}

// 0x000605f0 VmfThread::Dispatch
void VmfRouter::Dispatch(const uint8_t* pkt) const {
    if (pkt == 0) {
        return;
    }
    const uint8_t grp = pkt[0x14];
    for (ListenerList::const_iterator it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        if (it->second == grp) {
            it->first->OnVmfPacket(pkt);
        }
    }
}
