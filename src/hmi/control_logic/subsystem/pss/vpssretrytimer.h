// File and class are ours; no stock counterpart.
//
// DEVIATION: re-sends the PSS system setup block (0x4b) read while pss
// answers "not ready" (status 10). It lives on the VControlTask thread, so the
// VMF thread only emits SigSchedule (queued connection); the delay runs in a single-shot QTimer
// there, never inside VmfThread::Dispatch / RecieveVmfMsg.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PSS_VPSSRETRYTIMER_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PSS_VPSSRETRYTIMER_H

#include <QtCore/QObject>

class VPssRetryTimer : public QObject {
    Q_OBJECT
public:
    explicit VPssRetryTimer(int delay_ms);

    // Thread-safe: queues ScheduleSlot on this object's thread.
    void Schedule();

signals:
    void SigSchedule();

private slots:
    void ScheduleSlot();
    void FireSlot();

private:
    int m_delay_ms;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PSS_VPSSRETRYTIMER_H
