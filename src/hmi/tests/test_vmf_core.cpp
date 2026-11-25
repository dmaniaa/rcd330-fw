// File name is ours; stock name unknown.
#include "service/vmf/vmf_frame.h"
#include "tests/test_runner.h"

#include <string.h>
#include <vector>

static void test_pack_valid_wake_msg() {
    // VPowerProxy::SendWakeStateToPowerAgent(1): [grp=0x0b, sub=0x01, len_hi=0x00, len_lo=0x02, 0x6e, 0x01]
    const uint8_t raw[6] = {0x0b, 0x01, 0x00, 0x02, 0x6e, 0x01};
    uint8_t pkt[VMF_PKT_SIZE];
    CHECK(VmfPackMsg(pkt, raw, sizeof(raw)));
    CHECK_EQ(pkt[0], 0x02);
    CHECK_EQ(pkt[0x14], 0x0b);
    CHECK_EQ(pkt[0x15], 0x01);
    CHECK_EQ((uint16_t)(pkt[0x18] | (pkt[0x19] << 8)), 2u);
    CHECK_EQ(pkt[0x1a], 0x6e);
    CHECK_EQ(pkt[0x1b], 0x01);

    VmfUnpacked u;
    CHECK(VmfUnpackMsg(pkt, &u));
    CHECK_EQ(u.group, 0x0b);
    CHECK_EQ(u.sub, 0x01);
    CHECK_EQ(u.len, 2u);
    CHECK_EQ(u.payload[0], 0x6e);
    CHECK_EQ(u.payload[1], 0x01);
}

static void test_pack_rejects_malformed_inputs() {
    // Review Focus 2: NULL, <4 bytes, length mismatch, >0x4b4 payload overflow.
    uint8_t pkt[VMF_PKT_SIZE];
    CHECK(!VmfPackMsg(pkt, 0, 6));
    const uint8_t tiny[3] = {0x0b, 0x01, 0x00};
    CHECK(!VmfPackMsg(pkt, tiny, 3));
    const uint8_t mismatch[6] = {0x0b, 0x01, 0x00, 0x05, 0x6e, 0x01};
    CHECK(!VmfPackMsg(pkt, mismatch, 6));
    uint8_t huge[4 + VMF_MAX_PAYLOAD + 1];
    memset(huge, 0, sizeof(huge));
    huge[0] = 0x8f;
    huge[1] = 0x24;
    uint16_t bad_len = VMF_MAX_PAYLOAD + 1;
    huge[2] = (uint8_t)(bad_len >> 8);
    huge[3] = (uint8_t)(bad_len & 0xff);
    CHECK(!VmfPackMsg(pkt, huge, sizeof(huge)));
    // Zero-length payload is valid.
    const uint8_t zero_payload[4] = {0x0b, 0x05, 0x00, 0x00};
    CHECK(VmfPackMsg(pkt, zero_payload, 4));
    CHECK_EQ(pkt[0], 0x02);
    CHECK_EQ(pkt[0x14], 0x0b);
    CHECK_EQ((uint16_t)(pkt[0x18] | (pkt[0x19] << 8)), 0u);
}

struct MockListener : public IVmfListener {
    std::vector<uint8_t> seen_subs;
    void OnVmfPacket(const uint8_t* pkt) {
        VmfUnpacked u;
        if (VmfUnpackMsg(pkt, &u)) seen_subs.push_back(u.sub);
    }
};

static void test_router_group_dedup_and_fanout() {
    VmfRouter r;
    MockListener a, b;
    CHECK(r.AddGroup(0x70));
    CHECK(!r.AddGroup(0x70));  // duplicate ignored like 0x60720
    CHECK(r.AddGroup(0x0c));
    CHECK_EQ(r.Groups().size(), 2u);
    r.AddListener(&a, 0x70);
    r.AddListener(&b, 0x70);
    r.AddListener(&b, 0x0c);

    uint8_t pkt[VMF_PKT_SIZE];
    const uint8_t raw[5] = {0x70, 0x42, 0x00, 0x01, 0xaa};
    CHECK(VmfPackMsg(pkt, raw, sizeof(raw)));
    r.Dispatch(pkt);
    CHECK_EQ(a.seen_subs.size(), 1u);
    CHECK_EQ(a.seen_subs[0], 0x42);
    CHECK_EQ(b.seen_subs.size(), 1u);
    CHECK_EQ(b.seen_subs[0], 0x42);
}

int main() {
    RUN_TEST(test_pack_valid_wake_msg);
    RUN_TEST(test_pack_rejects_malformed_inputs);
    RUN_TEST(test_router_group_dedup_and_fanout);
    return test_summary();
}
