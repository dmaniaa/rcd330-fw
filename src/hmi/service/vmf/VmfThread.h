// Stock source: ../service/vmf/VmfThread.cpp (__assert_fail in 0x000604b8).
//
// Singleton VMF client: connects to opt/vmf, subscribes the groups the
// proxies registered, and fans each received packet out to the listeners of
// its group on its own thread.
#ifndef RCD330_HMI_SERVICE_VMF_VMFTHREAD_H
#define RCD330_HMI_SERVICE_VMF_VMFTHREAD_H

#include <stdint.h>

#include <string>

#include <QtCore/QMutex>
#include <QtCore/QThread>

#include "service/vmf/vmf_frame.h"

// No Q_OBJECT, unlike the plan's brief: stock vtable 0x003f5958 slots 0-2 are
// the QThread::metaObject/qt_metacast/qt_metacall PLT stubs, and moc output
// would import QThread::staticMetaObject, which no stock binary does.
class VmfThread : public QThread {
public:
    VmfThread();

    static VmfThread* Instance();

    // Client name passed to nw_vmf_connect; NULL is ignored. Stock never
    // calls it, so opt/hmi connects with "".
    void SetName(const char* name);
    // false when no handle after 10 attempts, or the group subscription fails.
    bool Connect();
    // Connect(), then QThread::start; aborts the process when Connect fails.
    void start();

    // Only before start(): false while running or for a duplicate group.
    bool AddGroup(uint8_t grp);
    void AddListener(IVmfListener* listener, uint8_t grp);
    void Dispatch(const uint8_t* pkt);
    // false without a connection or when nw_vmf_send fails.
    bool Send(const uint8_t* pkt);

protected:
    virtual void run();

private:
    signed char m_handle;   // +0x08, -1 = not connected
    VmfRouter m_router;     // +0x0c groups, +0x18 listeners
    std::string m_name;     // +0x20
    volatile bool m_stop;   // +0x24, never set by stock
    QMutex m_mutex;         // +0x28, guards the listener list
};

#endif // RCD330_HMI_SERVICE_VMF_VMFTHREAD_H
