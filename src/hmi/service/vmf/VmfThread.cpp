// Stock source: ../service/vmf/VmfThread.cpp
#include "service/vmf/VmfThread.h"

#include <string.h>

#include "service/osal/osal_api.h"
#include "service/vmf/vmf_api.h"

// glibc's assert() backend; <assert.h> hides it under NDEBUG, and stock calls
// it unconditionally (the abort message names the stock file and line).
extern "C" void __assert_fail(const char* assertion, const char* file, unsigned int line,
                              const char* function) throw() __attribute__((noreturn));

namespace {

const int kConnectAttempts = 10;
const unsigned long kConnectRetryMs = 2;
const short kConnectTimeoutMs = 100;
const unsigned short kConnectBufSize = 0xfe0;

} // namespace

// 0x000602e0 VmfThread::VmfThread
VmfThread::VmfThread() : QThread(0), m_handle(-1), m_stop(false) {}

// 0x00060384 VmfThread::Instance
VmfThread* VmfThread::Instance() {
    static VmfThread instance;
    return &instance;
}

// 0x000603f4 VmfThread::SetName
void VmfThread::SetName(const char* name) {
    if (name == 0) {
        return;
    }
    m_name.assign(name, strlen(name));
}

// 0x00060428 VmfThread::Connect
bool VmfThread::Connect() {
    // opt/vmf may still be starting: nw_vmf_connect fails until its server
    // socket exists, so retry with a short backoff (also after the last try,
    // like stock).
    for (int attempt = 0; attempt < kConnectAttempts; ++attempt) {
        const int handle = nw_vmf_connect(m_name.c_str(), 0, 0, kConnectTimeoutMs, kConnectBufSize);
        m_handle = static_cast<signed char>(handle);
        if (handle >= 0) {
            break;
        }
        QThread::msleep(kConnectRetryMs);
    }
    if (m_handle < 0) {
        // DEVIATION: stock fails silently here; log why start() will abort.
        log_simple("INFO", " ==== HMI ==== INFO: VmfThread: nw_vmf_connect failed %d times (last %d)\n",
                   kConnectAttempts, m_handle);
        return false;
    }
    // STOCK-QUIRK: with no registered group stock passes the empty vector's
    // NULL data pointer, which libvmf_client rejects with -12.
    const std::vector<uint8_t>& groups = m_router.Groups();
    const int ret = nw_vmf_register_msg_group(m_handle, static_cast<unsigned short>(groups.size()),
                                              groups.empty() ? 0 : &groups[0]);
    if (static_cast<short>(ret) < 0) {
        // DEVIATION: stock fails silently here.
        log_simple("INFO", " ==== HMI ==== INFO: VmfThread: nw_vmf_register_msg_group(%u groups) failed: %d\n",
                   static_cast<unsigned>(groups.size()), ret);
        return false;
    }
    return true;
}

// 0x000604b8 VmfThread::start
void VmfThread::start() {
    if (!Connect()) {
        // DEVIATION: stock asserts without a log line.
        log_simple("INFO", " ==== HMI ==== INFO: VmfThread: no VMF connection, aborting\n");
        __assert_fail("0", "../service/vmf/VmfThread.cpp", 0xd7, "void VmfThread::start()");
    }
    QThread::start(QThread::InheritPriority);
}

// 0x000604fc VmfThread::AddListener
void VmfThread::AddListener(IVmfListener* listener, uint8_t grp) {
    if (listener == 0) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    m_router.AddListener(listener, grp);
}

// 0x000605f0 VmfThread::Dispatch
void VmfThread::Dispatch(const uint8_t* pkt) {
    if (pkt == 0) {
        return;
    }
    // STOCK-QUIRK: listeners run with m_mutex held; one that calls
    // AddListener from its handler deadlocks (QMutex is not recursive).
    QMutexLocker lock(&m_mutex);
    m_router.Dispatch(pkt);
}

// 0x000606e8 VmfThread::Send
bool VmfThread::Send(const uint8_t* pkt) {
    if (pkt == 0 || m_handle == -1) {
        return false;
    }
    return static_cast<short>(nw_vmf_send(m_handle, pkt)) >= 0;
}

// 0x00060720 VmfThread::AddGroup
bool VmfThread::AddGroup(uint8_t grp) {
    // Groups are subscribed once in Connect(); later ones would never arrive.
    if (isRunning()) {
        return false;
    }
    return m_router.AddGroup(grp);
}

// No origin tag: stock run (0x00060688) is outside the decompiled function
// set; this body follows its disassembly. The
// receive size is VMF_RX_BUF_SIZE (stock passes 0x1008 as the buffer size,
// not a timeout), so nw_vmf_receive blocks until a packet arrives.
void VmfThread::run() {
    uint8_t buf[VMF_RX_BUF_SIZE];
    do {
        memset(buf, 0, sizeof(buf));
        if (nw_vmf_receive(m_handle, buf, sizeof(buf)) > 0) {
            Dispatch(buf);
        }
    } while (!m_stop);
}
