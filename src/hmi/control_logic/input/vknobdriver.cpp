// Stock source: control_logic/input/vknobdriver.cpp
#include "control_logic/input/vknobdriver.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <QtCore/QTimer>

#include "control_logic/input/vkeydriver.h"
#include "control_logic/vcontroltask.h"
#include "service/osal/osal_api.h"

#define KNOB_LOG(func, line, ...)                                                           \
    do {                                                                                    \
        log_simple("INFO", __VA_ARGS__);                                                    \
        printlog(3, "INFO", "control_logic/input/vknobdriver.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

const char kKnobDevice[] = "/dev/tuner_knob";
const int kFirstReadDelayMs = 200;

}  // namespace

// 0x000aa0d0 VKnobDriver::VKnobDriver
VKnobDriver::VKnobDriver() : m_word(0), m_fd(-1), m_logged_short(false), m_logged_word(false) {
    m_rotate.direction = 2;
    m_rotate.count = 0;
}

// 0x000aa1bc VKnobDriver::Init
// TODO(phase3): stock also registers the UINT8 typedef and connects
// SigKeyEventNotify(UINT8, UINT8) to VDisplayTask::SlotKeyEventNotify; no
// stock code emits that signal (0x003b2674 is reached only from the moc
// metacall), so neither is ported.
void VKnobDriver::Init() {
    connect(this, SIGNAL(SigInitialize()), this, SLOT(InitializeSlot()));
    connect(this, SIGNAL(switchToRead()), this, SLOT(readKnobDriver()), Qt::QueuedConnection);
}

// 0x000aa12c VKnobDriver::Run
void VKnobDriver::Run() {
    Start();
    emit SigInitialize();
}

// 0x000aa250 VKnobDriver::InitializeSlot
void VKnobDriver::InitializeSlot() {
    m_fd = open(kKnobDevice, O_RDONLY);
    if (m_fd < 0) {
        // Ours: stock returns without a log.
        KNOB_LOG("InitializeSlot", 0, " ==== HMI ==== ERROR: open %s failed: %s; no tune knob", kKnobDevice,
                 strerror(errno));
        return;
    }
    QTimer::singleShot(kFirstReadDelayMs, this, SLOT(readKnobDriver()));
}

// 0x000aa3e0 VKnobDriver::readKnobDriver
// TODO(phase3): the VDiagProxy detent reports (0x001ff080, 0x001fc48c).
void VKnobDriver::readKnobDriver() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);
    const int ready = select(m_fd + 1, &fds, 0, 0, 0);
    if (ready < 0 && errno != EINTR) {
        // DEVIATION: stock re-queues after any select() error and would spin.
        KNOB_LOG("readKnobDriver", 0, " ==== HMI ==== ERROR: select on %s failed: %s; tune knob stopped",
                 kKnobDevice, strerror(errno));
        return;
    }
    if (ready > 0 && FD_ISSET(m_fd, &fds)) {
        const ReadResult res = ReadKnobWord(m_fd, &m_word);
        if (res == kReadOk) {
            VKeyDriver* keys = VKeyDriver::Instance();
            // DEVIATION: stock dereferences VKeyDriver::Instance() unchecked.
            const bool tune_ok_down = keys != 0 && keys->IsTuneOkDown();
            uint8_t key = 0;
            if (!tune_ok_down) {
                if (DecodeKnobWord(m_word, &key)) {
                    PostDetent(key);
                    CalculateFastRotateNum(key == kHardKeyTuneUp ? 0 : 1);
                } else if (!m_logged_word) {
                    // Ours: stock drops unknown words silently.
                    m_logged_word = true;
                    KNOB_LOG("readKnobDriver", 0, " ==== HMI ==== WARN: unknown knob word 0x%x dropped (logged once)",
                             m_word);
                }
            }
        } else if (res == kReadShort) {
            // Stock logs every failed read; ours logs once.
            if (!m_logged_short) {
                m_logged_short = true;
                KNOB_LOG("readKnobDriver", 0xe0,
                         " ==== HMI ==== INFO: Error in reading knob value, returnValue = %d (logged once)\n", -1);
            }
        } else {
            // DEVIATION: stock logs and re-queues after EOF / errors and would spin.
            KNOB_LOG("readKnobDriver", 0xe0, " ==== HMI ==== INFO: Error in reading knob value (%s); tune knob stopped\n",
                     res == kReadEof ? "EOF" : strerror(errno));
            return;
        }
    }
    emit switchToRead();
}

void VKnobDriver::PostDetent(uint8_t key) {
    VControlTask* control = VControlTask::Instance();
    if (control != 0) {
        control->PostKeyEvent(key, 1);
    }
}

// 0x000aa304 VKnobDriver::CalculateFastRotateNum
// TODO(phase3): stock skips the count while screen 0xfe is current and its
// page (0x0002b8b8) reports +0x68 or +0x6c (VPageManager, Task 9+).
void VKnobDriver::CalculateFastRotateNum(uint8_t direction) {
    bool restart = false;
    const int elapsed = m_rotate_time.isNull() ? 0x7fffffff : m_rotate_time.elapsed();
    const uint32_t extra = ::CalculateFastRotateNum(m_rotate, direction, elapsed, &restart);
    if (restart) {
        m_rotate_time.restart();
    }
    if (extra != 0) {
        DuplicateRotation(direction, extra);
    }
}

// 0x000aa2b8 VKnobDriver::DuplicateRotation
// count is already multiplied by 7 (see input_decoders.h).
void VKnobDriver::DuplicateRotation(uint8_t direction, uint32_t count) {
    const uint8_t key = KnobDirectionKey(direction);
    for (uint32_t i = 0; i < count; ++i) {
        PostDetent(key);
    }
}
