// Stock source: control_logic/subsystem/pss/vpssproxy.cpp
#include "control_logic/subsystem/pss/vpssproxy.h"

#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "control_logic/subsystem/phase3_stubs.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "control_logic/subsystem/proxy_decoders.h"
#include "control_logic/subsystem/pss/vpssretrytimer.h"
#include "service/osal/osal_api.h"

namespace {

// 0x0096a2cc kGroups
const uint8_t kGroups[1] = {0x72};

VPSSProxy* g_pSSProxy = 0;

const char kFile[] = "control_logic/subsystem/pss/vpssproxy.cpp";

#define PSS_LOG(func, line, fmt)            \
    do {                                    \
        log_simple("INFO", fmt);            \
        printlog(3, "INFO", kFile, func, line, fmt); \
    } while (0)

} // namespace

// TODO(phase3): stock member state.
// 0x001c3338 VPSSProxy::VPSSProxy
VPSSProxy::VPSSProxy() : m_logged(false), m_retry(0), m_logged_no_retry_timer(false) {
    ResetSetupReadRetry(&m_setup_retry);
}

// 0x001c338c VPSSProxy::Instance
VPSSProxy* VPSSProxy::Instance() {
    if (g_pSSProxy == 0) {
        g_pSSProxy = new VPSSProxy();
    }
    return g_pSSProxy;
}

// 0x001c33d4 VPSSProxy::Init
void VPSSProxy::Init() {
    RegisterVmfGroup(kGroups, sizeof(kGroups));
}

// Ours; see the header.
void VPSSProxy::SetSetupReadRetryThread(QThread* thread) {
    if (thread == 0 || m_retry != 0) {
        return;
    }
    m_retry = new VPssRetryTimer(kSetupReadRetryMs);
    m_retry->moveToThread(thread);
}

// 0x001c3ce0 VPSSProxy::OnSendPSSReadSystemSetupStatusRequest
void VPSSProxy::OnSendPSSReadSystemSetupStatusRequest() {
    // DEVIATION: a fresh request restarts the status-10 retry budget. Its
    // only caller (PA_OUT_VRM_INTO) runs on the VMF thread, like the replies.
    ResetSetupReadRetry(&m_setup_retry);
    SendSystemSetupReadMsg();
}

// Ours; see the header.
void VPSSProxy::ResendSystemSetupStatusRequest() {
    SendSystemSetupReadMsg();
}

// No origin tag: the stock body of 0x001c3ce0, shared by the fresh request
// and the re-send.
void VPSSProxy::SendSystemSetupReadMsg() {
    PSS_LOG("OnSendPSSReadSystemSetupStatusRequest", 0x631,
            " ==== HMI ==== INFO: VPSSProxy::OnSendPSSReadSystemSetupStatusRequest");
    SendVmfMsg(kPssReadSystemSetupMsg, sizeof(kPssReadSystemSetupMsg));
}

// 0x001c5a90 VPSSProxy::RecieveVmfMsg
// Phase 2 ports only the system setup block (0x4b) read reply: it decides
// region and language. STOCK-QUIRK: when the read fails (always under QEMU),
// the region follows the App-Connect coding as decoded at that moment, so
// stock and ours land on en_US or zh_CN depending on whether diag's coding
// reached PSS first.
// TODO(phase3): the other blocks, the rest of the 0x4b replies (setup
// fields, the setup object 0x001ffc10), and the post to VDisplayTask
// (0x000633a4 -> 0x00064278) every reply ends with.
void VPSSProxy::RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) {
    if (data == 0) {
        return;
    }
    if (grp == kGroups[0] && sub == kPssReadReplySub && len >= 2 && data[0] == kPssBlkSystemSetupNew) {
        // Any other status ends a retry episode (OnSetupReadReply resets).
        const SetupReadRetryStep step = OnSetupReadReply(&m_setup_retry, data[1]);
        if (data[1] == kPssStatusNotReady) {
            // DEVIATION: pss is not loaded yet (status 10, see
            // kPssStatusNotReady); stock ignores this reply because it asks
            // later. Re-send off the VMF thread, bounded per fresh request.
            if (m_retry == 0) {
                if (!m_logged_no_retry_timer) {
                    m_logged_no_retry_timer = true;
                    log_simple("INFO", " ==== HMI ==== INFO: VPSSProxy: setup block 0x4b not ready (status %d), "
                               "no retry timer", data[1]);
                }
            } else if (step == kSetupReadRetryFirst || step == kSetupReadRetry) {
                if (step == kSetupReadRetryFirst) {
                    log_simple("INFO", " ==== HMI ==== INFO: VPSSProxy: setup block 0x4b not ready (status %d), "
                               "retrying every %d ms (max %d)", data[1], kSetupReadRetryMs, kSetupReadMaxRetries);
                }
                m_retry->Schedule();
            } else if (step == kSetupReadGiveUp) {
                log_simple("INFO", " ==== HMI ==== INFO: VPSSProxy: setup block 0x4b still not ready after %d "
                           "retries, giving up", m_setup_retry.retries);
            }
            return;
        }
        const PssReadOutcome outcome = ClassifyPssReadStatus(data[1]);
        if (outcome == kPssReadOk && len > 0xb) {
            VDisplayProxy::Instance()->SetRegion(data[0xb]);
        } else if (outcome == kPssReadFailed) {
            PSS_LOG("RecieveVmfMsg", 0x115,
                    " ==== HMI ==== INFO: VPSSProxy::HMI_SYSTEM_SETUP_PSS_NEWBLK: PSS_STATUS_READ_FAILED ");
            const uint8_t region = SetupNewBlkFailedRegion(VAppConnectProxy::Instance()->GetRegionMode(),
                                                           VPowerProxy::Instance()->GetPlatformType());
            VDisplayProxy::Instance()->ApplyRegion(region);
        }
        return;
    }
    ProxyLogUnhandledOnce(&m_logged, "VPSSProxy", grp, sub);
}
