// File name is ours; stock name unknown.
// Stock base class for QThread-backed HMI tasks (0x000609f8..0x00060ae8).
#ifndef RCD330_HMI_SERVICE_UTILITY_VBASETASK_H
#define RCD330_HMI_SERVICE_UTILITY_VBASETASK_H

#include <QtCore/QObject>
#include <QtCore/QThread>

class VBaseTask : public QObject {
    Q_OBJECT
public:
    VBaseTask();
    virtual ~VBaseTask();

    void Start();       // 0x00060ac8
    void LogThread();   // 0x00060ae8
    QThread* Thread() { return &m_thread; }

signals:
    void SigInitialize();

protected slots:
    virtual void InitializeSlot();

protected:
    QThread m_thread;
};

#endif
