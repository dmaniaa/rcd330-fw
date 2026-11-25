// File name is ours; stock name unknown.
#include "service/utility/vbasetask.h"

// 0x000609f8 VBaseTask::VBaseTask
VBaseTask::VBaseTask() : QObject(0), m_thread(0) {
    moveToThread(&m_thread);
}

// 0x00060a50 VBaseTask::~VBaseTask
VBaseTask::~VBaseTask() {
    // Stock calls only QThread::exit(0); device stock binaries do not import QThread::wait().
    m_thread.exit(0);
}

// 0x00060ac8 VBaseTask::Start
void VBaseTask::Start() {
    m_thread.start(QThread::InheritPriority);
}

// 0x00060ae8 VBaseTask::LogThread
void VBaseTask::LogThread() {
    (void)m_thread.stackSize();
    (void)m_thread.priority();
}

void VBaseTask::InitializeSlot() {}
