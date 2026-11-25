// Stock source: control_logic/subsystem/power/vpowerproxy.cpp (printlog in
// 0x00056040).
//
// Power agent / VIP power proxy: tracks the pwr_agent power mode, ignition,
// platform type and VIP audio mode, switches the backlight, and hands every
// handled message on to VControlTask::PowersubsystemNotifyMsg (0x00034630)
// through IPowerNotifySink so this proxy does not depend on VControlTask.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_POWER_VPOWERPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_POWER_VPOWERPROXY_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

// Bound by VControlTask::Init (Task 6). Stock calls
// VControlTask::Instance()->PowersubsystemNotifyMsg(grp, sub, data, len) at
// the end of VPowerProxy::RecieveVmfMsg; that builds the FILTER_MSG for
// VPowerNotifyFilter (e.g. HandleAudioModeStatus 0x00046054 for 0x74:0x01).
// PowersubsystemNotifyMsg runs on the VMF thread, from VmfThread::Dispatch
// with the listener mutex held: implementations must post the message to
// their own task instead of working inline, and must never register VMF
// groups or add listeners (that would deadlock).
class IPowerNotifySink {
public:
    virtual ~IPowerNotifySink() {}
    virtual void PowersubsystemNotifyMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) = 0;
};

class VPowerProxy : public VBaseProxy {
public:
    static VPowerProxy* Instance();

    void Init();
    // NULL restores the default no-op sink.
    void SetPowerNotifySink(IPowerNotifySink* sink);

    void SendWakeStateToPowerAgent(int wake_state);
    void SetBacklightOnOFF(uint8_t on);
    void SetHMIOn();
    void SetHMIOff();
    // Backlight on unless the over-temperature logic holds it off.
    void RestoreBacklight();
    // Picks the page to show again after power-up (stock RecoverSetupMainPage).
    void RecoverSetupMainPage();

    uint8_t GetPlatformType() const;
    // Stock name; returns the backlight state SetBacklightOnOFF stored (+0x85).
    uint8_t IsHMIOn() const;
    void SetRvcCoded(uint8_t coded);
    void SetStartupDone(uint8_t state);
    uint8_t GetAuxMode() const;
    uint8_t GetPowerState() const;
    void GetTimeDisplayFlags(uint8_t* f0, uint8_t* f1, uint8_t* f2) const;
    int GetRecoverPage() const;
    uint8_t GetPhoneWakeFlag() const;
    uint8_t GetRemoteWakeFlag() const;
    // +0x8d, set by the temperature-protection power mode.
    uint8_t GetTempProtect() const;
    // Asks pwr_agent for the current power state ({0b 03 00 03 0c 01 02}).
    void GetPwrCurStateToPowerAgent();
    // +0x04: set while the HMI is running (RUN / TEMP_ON), read by
    // VDisplayProxy and VAudioProxy.
    uint8_t GetHmiRunFlag() const { return m_hmi_run; }
    // +0x06, read inline by stock (e.g. VAudioProxy::InsertAudioMode).
    uint8_t GetStartupState() const { return m_startup_state; }

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VPowerProxy();
    // Both return false where stock returns before the notify.
    bool HandleModeChange(const uint8_t* data, uint16_t len);
    bool HandleRunMode(uint8_t mode);

    IPowerNotifySink* m_notify_sink;
    uint8_t m_hmi_run;            // +0x04
    // +0x06: 1 = WS_IDLE / not started, 2 = startup done. main() writes 1
    // before VmfThread::start(); every later access is on the VMF thread.
    uint8_t m_startup_state;
    uint8_t m_power_state;        // +0x1a: last PA_OUT_MODE_CHG mode
    uint8_t m_run_state;          // +0x1b
    uint8_t m_grp0d_0;            // +0x1c
    uint8_t m_grp0d_1;            // +0x1d
    uint8_t m_aux_mode;           // +0x1e
    uint8_t m_running_init;       // +0x16: 1 after WS_RUNNING
    uint8_t m_audio_mode;         // +0x64: VIP audio mode (0x74:0x01 0x81)
    uint8_t m_acc_status;         // +0x65
    uint8_t m_ignition_status;    // +0x66
    uint8_t m_time_flags[3];      // +0x80..0x82
    uint8_t m_platform_type;      // +0x83
    uint8_t m_platform_sub;       // +0x84
    uint8_t m_backlight_on;       // +0x85
    uint8_t m_vrm_info;           // +0x86
    uint8_t m_rvc_coded;          // +0x8a
    uint8_t m_temp_protect;       // +0x8d
    uint8_t m_remote_wake;        // +0x8e
    uint8_t m_phone_wake;         // +0x8f
    uint8_t m_ign_mode_c;         // +0x9b: mode == 0x0c
    int m_recover_page;           // +0xa0
    uint8_t m_first_wake_reason;  // +0xa7
    uint8_t m_wake_reason;        // +0xaf
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_POWER_VPOWERPROXY_H
