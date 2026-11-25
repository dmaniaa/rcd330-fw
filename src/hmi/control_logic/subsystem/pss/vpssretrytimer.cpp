// File and class are ours; see vpssretrytimer.h.
#include "control_logic/subsystem/pss/vpssretrytimer.h"

#include <QtCore/QTimer>

#include "control_logic/subsystem/pss/vpssproxy.h"

VPssRetryTimer::VPssRetryTimer(int delay_ms) : QObject(0), m_delay_ms(delay_ms) {
    connect(this, SIGNAL(SigSchedule()), this, SLOT(ScheduleSlot()), Qt::QueuedConnection);
}

void VPssRetryTimer::Schedule() {
    emit SigSchedule();
}

void VPssRetryTimer::ScheduleSlot() {
    QTimer::singleShot(m_delay_ms, this, SLOT(FireSlot()));
}

void VPssRetryTimer::FireSlot() {
    VPSSProxy::Instance()->ResendSystemSetupStatusRequest();
}
