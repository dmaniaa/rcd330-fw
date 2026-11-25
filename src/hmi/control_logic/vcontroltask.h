// Stock source: control_logic/vcontroltask.cpp
//
// Control-logic task: owns the notify filter chain, receives power / VMF
// notifications (IPowerNotifySink) and driver hard keys, and runs the
// filters on its own thread. It is also the filters' IFilterActions, which
// it forwards to the proxies, VPageManager (IPageQuery, bound by Task 9)
// and its key timer.
//
// Threads:
// - PowersubsystemNotifyMsg runs on the VMF thread inside VmfThread::Dispatch
//   (listener mutex held). It only updates proxy state and emits
//   SigPowerSubsystemNotifyMsg, which Qt queues to this task's thread; it
//   never registers VMF groups or adds listeners.
// - PostKeyEvent may be called from any thread (input drivers); it queues
//   the key to KeyDriverNotifySlot on this task's thread.
// - KeySubsystemNotifyMsg (VDisplayTask re-posting a key no page consumed)
//   may be called from any thread; the signal is queued.
#ifndef RCD330_HMI_CONTROL_LOGIC_VCONTROLTASK_H
#define RCD330_HMI_CONTROL_LOGIC_VCONTROLTASK_H

#include <stdint.h>

#include "control_logic/mode/ifilteractions.h"
#include "control_logic/mode/vbasefilter.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "service/utility/vbasetask.h"

class QTimer;
class VAudioNotifyFilter;
class VCameraNotifyFilter;
class VDisplayCmdFilter;
class VIPCLNotifyFilter;
class VMediaNotifyFilter;
class VPowerNotifyFilter;
class VPrehandleFilter;

// VPageManager queries the filters need (IGDT screen ids). Task 9's
// VPageManager implements it; unbound, no screen is shown.
class IPageQuery {
public:
    virtual ~IPageQuery() {}
    virtual int GetCurrentScreen() const = 0;              // 0x0002bb60
    virtual int GetBaseScreen() const = 0;                 // 0x0002bb70
    virtual bool IsScreenShown(uint8_t scr_id) const = 0;  // 0x0002b8b8
    virtual bool IsScreenInStack(uint8_t scr_id) const = 0;  // 0x0002bbf8
};

class VControlTask : public VBaseTask, public IPowerNotifySink, public IFilterActions {
    Q_OBJECT
public:
    VControlTask();
    virtual ~VControlTask();

    // Stock returns the object the constructor registered (NULL before);
    // it does not create one.
    static VControlTask* Instance();

    // Proxy Instance()/Init() in stock order, filter metatype and signal
    // connections, then SigInitialize. Call before VmfThread::start().
    void Init();
    // Starts the task thread.
    void Run();

    // Any thread. code: VKeyDriver / VKnobDriver key code (1..10);
    // state: 1 pressed, 0 released.
    void PostKeyEvent(uint8_t code, uint8_t state);
    // Any thread. VDisplayTask re-posts {2, 4, {4, 1, key, state}} here when
    // no IGDT page consumed SigKeyEventNotify(key, state).
    void KeySubsystemNotifyMsg(const FILTER_MSG& msg);

    // NULL unbinds.
    void SetPageQuery(IPageQuery* query);

    // IPowerNotifySink (VMF thread).
    virtual void PowersubsystemNotifyMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

    // IFilterActions (task thread).
    virtual void SetMainPage(int audio_src, int force);
    virtual void InsertPage(int page_id);
    virtual void ClosePopPage(uint8_t pop_id);
    virtual void NotifyHmiOn();
    virtual int GetCurDisplayPage() const;
    virtual bool IsIgdtShown() const;
    virtual void UpdateBacklight();
    virtual int GetCurrentScreen() const;
    virtual int GetBaseScreen() const;
    virtual bool IsScreenShown(uint8_t scr_id) const;
    virtual bool IsScreenInStack(uint8_t scr_id) const;
    virtual int GetAudioMode() const;
    virtual void SetAudioMode(int mode);
    virtual void QueryTunerInfo();
    virtual int GetTunerBand() const;
    virtual int GetTunerState() const;
    virtual bool IsHMIOn() const;
    virtual bool IsTempProtect() const;
    virtual int GetAuxMode() const;
    virtual int GetRegionMode() const;
    virtual int GetAppDeviceType() const;
    virtual int GetMediaDeviceFlags() const;
    virtual int GetMirrorLinkState() const;
    virtual void StartKeyTimer(int ms);
    virtual void StopKeyTimer();
    virtual int GetActiveKeyCode() const;
    virtual bool IsMultiHardkeyPermit() const;
    virtual void NotifyDisplayKey(uint8_t key, uint8_t state);
    virtual void NotifyBapKey(uint8_t key, uint8_t state);

signals:
    void SigPowerSubsystemNotifyMsg(FILTER_MSG msg);
    void SigKeySubsystemNotifyMsg(FILTER_MSG msg);
    void SigRemoveHighestPage();
    // Stock signature is (UINT8, UINT8); unsigned char needs no typedef
    // registration for queued connections.
    void SigKeyEventNotify(unsigned char key, unsigned char state);
    void SigIgdtRefreshNotify(unsigned char scr_id);
    // Ours: carries PostKeyEvent to the task thread.
    void SigKeyDriverNotify(unsigned short code, int value);

protected slots:
    virtual void InitializeSlot();

private slots:
    void KeyDriverNotifySlot(unsigned short code, int value);
    void KeysubsystemNotifySlot(FILTER_MSG msg);
    void PowersubsystemNotifySlot(FILTER_MSG msg);
    void HMIKeyTimeOutSlot();
    void OnIgdtRefreshNotify(unsigned char scr_id);

private:
    void RegisterSignals();
    void EmitPowerMsg(const FILTER_MSG& msg);

    QTimer* m_key_timer;              // +0x14
    int m_key_tick_ms;                // +0x1c
    VAudioNotifyFilter* m_audio_notify;   // +0x24
    VIPCLNotifyFilter* m_ipcl_notify;     // +0x30
    VPrehandleFilter* m_prehandle;        // +0x34
    VPowerNotifyFilter* m_power_notify;   // +0x38
    VMediaNotifyFilter* m_media_notify;   // +0x44
    VCameraNotifyFilter* m_camera_notify; // +0x58
    VDisplayCmdFilter* m_display_cmd;     // +0x28
    // VMF thread only: last 0x0c:0x04 device-status fields (+0xd1..+0xd4).
    uint8_t m_dev_sts_b45;  // +0xd1
    uint8_t m_dev_sts_b01;  // +0xd2
    uint8_t m_dev_sts_b23;  // +0xd3
    uint8_t m_dev_sts_b67;  // +0xd4
    IPageQuery* m_page_query;
    bool m_logged_bad_key;     // ours: log-once flags
    bool m_logged_bap_key;
};

#endif // RCD330_HMI_CONTROL_LOGIC_VCONTROLTASK_H
