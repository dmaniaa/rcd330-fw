// Stock source: ../service/utility/vbaseproxy.cpp
#include "service/utility/vbaseproxy.h"

#include "service/osal/osal_api.h"
#include "service/vmf/VmfThread.h"

namespace {

const char kShortBufferMsg[] =
    " ==== HMI ==== INFO: bt_proxy@Warning:try to send a data buffer which size is less than 4!!!\n";
const char kEmptyBufferMsg[] = " ==== HMI ==== INFO: VBaseProxy@error:try to send a empty data buffer!!!\n";

} // namespace

// 0x0005fdb4 VBaseProxy::VBaseProxy
VBaseProxy::VBaseProxy() {}

// 0x0005fd24 VBaseProxy::~VBaseProxy
VBaseProxy::~VBaseProxy() {}

// 0x0005fdd4 VBaseProxy::RegisterVmfGroup
void VBaseProxy::RegisterVmfGroup(const uint8_t* groups, uint32_t count) {
    if (groups == 0 || count == 0) {
        return;
    }
    // DEVIATION: stock counts with a uint8_t index, so count > 255 never
    // terminates; no proxy registers more than a handful of groups.
    for (uint32_t i = 0; i < count; ++i) {
        VmfThread::Instance()->AddListener(this, groups[i]);
    }
    for (uint32_t i = 0; i < count; ++i) {
        // AddGroup is also false for a group another proxy already added,
        // which is normal (0x70 has five listeners).
        if (!VmfThread::Instance()->AddGroup(groups[i]) && VmfThread::Instance()->isRunning()) {
            // DEVIATION: stock ignores the result; a group added after
            // VmfThread::start() is never subscribed, so say so.
            log_simple("INFO", " ==== HMI ==== INFO: VBaseProxy: group 0x%02x registered after VmfThread start, "
                               "never subscribed\n",
                       groups[i]);
        }
    }
}

// 0x0005fe68 VBaseProxy::PackMsg
bool VBaseProxy::PackMsg(uint8_t out[VMF_PKT_SIZE], const uint8_t* buf, uint32_t len) {
    if (VmfPackMsg(out, buf, len)) {
        return true;
    }
    if (buf != 0 && len < 4) {
        // STOCK-QUIRK: every proxy logs the "bt_proxy" prefix.
        log_simple("INFO", kShortBufferMsg);
        printlog(3, "INFO", "../service/utility/vbaseproxy.cpp", "PackMsg", 0xa4, kShortBufferMsg);
    } else if (buf != 0) {
        // DEVIATION: stock drops a bad length without a log line.
        log_simple("INFO", " ==== HMI ==== INFO: VBaseProxy: drop grp 0x%02x sub 0x%02x: length field %u, "
                           "buffer %u (max payload %u)\n",
                   buf[0], buf[1], (static_cast<unsigned>(buf[2]) << 8) | buf[3], len,
                   static_cast<unsigned>(VMF_MAX_PAYLOAD));
    }
    return false;
}

// DEVIATION: stock returns nothing; ours reports whether the packet left so
// a caller can log a send that went nowhere.
// 0x0005ff4c VBaseProxy::SendVmfMsg
bool VBaseProxy::SendVmfMsg(const uint8_t* buf, uint32_t len) {
    if (buf == 0) {
        log_simple("INFO", kEmptyBufferMsg);
        printlog(3, "INFO", "../service/utility/vbaseproxy.cpp", "SendVmfMsg", 0xdc, kEmptyBufferMsg);
        return false;
    }
    uint8_t pkt[VMF_PKT_SIZE];
    // DEVIATION: stock ignores PackMsg's result and sends the zero-filled
    // packet, which nw_vmf_send forwards as a basic packet to group 0.
    if (!PackMsg(pkt, buf, len)) {
        return false;
    }
    return VmfThread::Instance()->Send(pkt);
}

// No origin tag: stock OnVmfPacket (0x0005fd6c) is outside the decompiled
// function set; this body follows its disassembly.
void VBaseProxy::OnVmfPacket(const uint8_t* pkt) {
    VmfUnpacked u;
    if (!VmfUnpackMsg(pkt, &u)) {
        return;
    }
    RecieveVmfMsg(u.group, u.sub, u.payload, u.len);
}

void VBaseProxy::RecieveVmfMsg(uint8_t, uint8_t, const uint8_t*, uint16_t) {}
