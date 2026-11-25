// File name is ours; stock name unknown.
//
// VMF basic-packet framing and listener routing used by VBaseProxy and
// VmfThread. Portable and host-testable: no Qt, no libvmf_client, no locking
// (VmfThread holds its QMutex around the router).
//
// Basic packet (docs/ipc-and-vgw.md): [0] = 0x02, [0x14] = group,
// [0x15] = sub, little-endian uint16 payload length at [0x18], payload at
// [0x1a]. Proxies hand PackMsg a raw buffer [group, sub, len_hi, len_lo,
// payload...] with a big-endian length.
#ifndef RCD330_HMI_SERVICE_VMF_VMF_FRAME_H
#define RCD330_HMI_SERVICE_VMF_VMF_FRAME_H

#include <stdint.h>

#include <list>
#include <utility>
#include <vector>

enum {
    VMF_PKT_SIZE = 0x4d0,      // TX packet buffer of VBaseProxy::SendVmfMsg (0x0005ff4c)
    VMF_HDR_SIZE = 0x1a,
    VMF_MAX_PAYLOAD = 0x4b4,   // VMF_PKT_SIZE - VMF_HDR_SIZE, PackMsg's limit
    VMF_RX_BUF_SIZE = 0x1008,  // RX buffer of VmfThread::run (0x00060688)
    VMF_RX_MAX_PAYLOAD = VMF_RX_BUF_SIZE - VMF_HDR_SIZE
};

// VBaseProxy::PackMsg without the logging: zero-fills out_pkt, then frames
// raw_buf into it. false (out_pkt left all-zero) for a NULL buffer, fewer
// than 4 bytes, a payload over VMF_MAX_PAYLOAD, or a big-endian length that
// is not raw_len - 4.
bool VmfPackMsg(uint8_t out_pkt[VMF_PKT_SIZE], const uint8_t* raw_buf, uint32_t raw_len);

struct VmfUnpacked {
    uint8_t group;
    uint8_t sub;
    uint16_t len;
    const uint8_t* payload;  // points into the packet
};

// Splits a received basic packet; false for NULL, a non-basic packet
// (pkt[0] != 2) or a length past the VMF_RX_BUF_SIZE receive buffer.
bool VmfUnpackMsg(const uint8_t pkt[VMF_PKT_SIZE], VmfUnpacked* out);

// Stock RTTI "7VmfBase" (typeinfo base of 10VBaseProxy): the interface
// VmfThread::Dispatch calls through vtable slot 2.
class VmfBase {
public:
    virtual ~VmfBase() {}
    virtual void OnVmfPacket(const uint8_t* pkt) = 0;
};

// The name the Phase 2 plan uses for VmfBase.
typedef VmfBase IVmfListener;

// VmfThread's routing state: the unique subscribed groups (std::vector at
// +0xc) and the (listener, group) list (std::list at +0x18).
class VmfRouter {
public:
    // false when grp is already registered (0x00060720 dedup).
    bool AddGroup(uint8_t grp);
    // Appends (listener, grp); a NULL listener is ignored (0x000604fc).
    void AddListener(IVmfListener* listener, uint8_t grp);
    // Calls OnVmfPacket of every listener registered for pkt[0x14], in
    // registration order (0x000605f0). A NULL pkt is ignored.
    void Dispatch(const uint8_t* pkt) const;

    const std::vector<uint8_t>& Groups() const { return m_groups; }

private:
    typedef std::list<std::pair<IVmfListener*, uint8_t> > ListenerList;

    std::vector<uint8_t> m_groups;
    ListenerList m_listeners;
};

#endif // RCD330_HMI_SERVICE_VMF_VMF_FRAME_H
