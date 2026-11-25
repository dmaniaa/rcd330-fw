// File name is ours; stock name unknown.
//
// Phase 3 proxies, reduced to their stock singleton + Init so
// VControlTask::Init (0x00033c68) registers the complete stock VMF group list
// before VmfThread::start(). Their RecieveVmfMsg logs the first message once
// and drops the rest (Review Focus 5).
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PHASE3_STUBS_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PHASE3_STUBS_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

// Logs "<proxy>: group/sub not handled in phase 2" the first time; *logged
// keeps it to one line per proxy.
void ProxyLogUnhandledOnce(bool* logged, const char* proxy, uint8_t grp, uint8_t sub);

// Stock: control_logic/subsystem/bt/vbtproxy.cpp
class VBTProxy : public VBaseProxy {
public:
    static VBTProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VBTProxy() : m_logged(false) {}
    bool m_logged;
};

// Stock: control_logic/subsystem/mirrorlink/vmirrorlinkproxy.cpp
class VMirrorLinkProxy : public VBaseProxy {
public:
    static VMirrorLinkProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VMirrorLinkProxy() : m_logged(false) {}
    bool m_logged;
};

// Stock: control_logic/subsystem/carplay/vcarplayproxy.cpp
class VCarPlayProxy : public VBaseProxy {
public:
    static VCarPlayProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VCarPlayProxy() : m_logged(false) {}
    bool m_logged;
};

// Stock: control_logic/subsystem/carlife/vcarlifeproxy.cpp
class VCarLifeProxy : public VBaseProxy {
public:
    static VCarLifeProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VCarLifeProxy() : m_logged(false) {}
    bool m_logged;
};

// Stock: control_logic/subsystem/androidauto/vandroidautoproxy.cpp
class VAndroidAutoProxy : public VBaseProxy {
public:
    static VAndroidAutoProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VAndroidAutoProxy() : m_logged(false) {}
    bool m_logged;
};

// Stock: control_logic/subsystem/service/vserviceproxy.cpp (groups 0xbe,
// 0x6f, 0x80).
class VServiceProxy : public VBaseProxy {
public:
    static VServiceProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VServiceProxy() : m_logged(false) {}
    bool m_logged;
};

// Class name is ours; stock name unknown. The second service-area proxy
// (Instance 0x00200ec8, groups 0x1f, 0x70) that docs/hmi-internals.md lists
// under VServiceProxy; VPowerProxy calls it on ignition (0x0020128c).
class VServiceAuxProxy : public VBaseProxy {
public:
    static VServiceAuxProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VServiceAuxProxy() : m_logged(false) {}
    bool m_logged;
};

// Stock: control_logic/subsystem/test/vsubsystemproxy.cpp
class VSubsystemProxy : public VBaseProxy {
public:
    static VSubsystemProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VSubsystemProxy() : m_logged(false) {}
    bool m_logged;
};

// Class name is ours; stock name unknown. App-Connect popup listener
// (Instance 0x003aba80, groups 0xc4, 0xd2).
class VAppConnectPopupProxy : public VBaseProxy {
public:
    static VAppConnectPopupProxy* Instance();
    void Init();
    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);
private:
    VAppConnectPopupProxy() : m_logged(false) {}
    bool m_logged;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_PHASE3_STUBS_H
