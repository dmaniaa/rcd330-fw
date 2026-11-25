// File name is ours; stock name unknown.
//
// What the notify/command filters ask of the rest of the HMI. Stock filters
// call the proxy singletons, VPageManager and VControlTask directly; here
// they go through this interface so control_logic/mode builds and is tested
// on the host without Qt or VMF. VControlTask implements it on target.
// Each method names the stock function it stands for.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_IFILTERACTIONS_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_IFILTERACTIONS_H

#include <stdint.h>

class IFilterActions {
public:
    virtual ~IFilterActions() {}

    // VDisplayProxy
    virtual void SetMainPage(int audio_src, int force) = 0;  // 0x00054fcc
    virtual void InsertPage(int page_id) = 0;                // 0x00054314
    virtual void ClosePopPage(uint8_t pop_id) = 0;           // 0x00053b98 ClosePage
    virtual void NotifyHmiOn() = 0;                          // 0x000500b4
    virtual int GetCurDisplayPage() const = 0;               // 0x00050590
    // +0xc8, set by the first hmi_wrapper_igdt_show (0x00060f54).
    virtual bool IsIgdtShown() const = 0;                    // 0x000517fc
    virtual void UpdateBacklight() = 0;                      // 0x00050bcc

    // VPageManager (Task 9): IGDT screen ids.
    virtual int GetCurrentScreen() const = 0;                // 0x0002bb60
    virtual int GetBaseScreen() const = 0;                   // 0x0002bb70
    virtual bool IsScreenShown(uint8_t scr_id) const = 0;    // 0x0002b8b8
    virtual bool IsScreenInStack(uint8_t scr_id) const = 0;  // 0x0002bbf8

    // VAudioProxy
    virtual int GetAudioMode() const = 0;                    // 0x00046fb8
    virtual void SetAudioMode(int mode) = 0;                 // 0x0004875c InsertAudioMode

    // VAMFMProxy
    // OnGetCurrentInfoOpera 0x000ad104 + OnGetTunerListOpera 0x000ad1a4 +
    // OnGetSeekmodeOpera 0x000ae1f0, always called together by the filters.
    virtual void QueryTunerInfo() = 0;
    virtual int GetTunerBand() const = 0;                    // 0x000af314 (+0x12)
    virtual int GetTunerState() const = 0;                   // 0x000af304 (+0x14)

    // VPowerProxy
    virtual bool IsHMIOn() const = 0;                        // 0x0005a0b8
    virtual bool IsTempProtect() const = 0;                  // 0x0005b9cc
    virtual int GetAuxMode() const = 0;                      // 0x000563bc

    // VAppConnectProxy
    virtual int GetRegionMode() const = 0;                   // 0x00347498
    virtual int GetAppDeviceType() const = 0;                // 0x00347580 (+0x1c)

    // Phase-3 subsystem state the filters read.
    virtual int GetMediaDeviceFlags() const = 0;             // VMediaProxy 0x000d7088
    virtual int GetMirrorLinkState() const = 0;              // VMirrorLinkProxy 0x000dca70

    // VControlTask / VKeyDriver / igdt_wrapper
    virtual void StartKeyTimer(int ms) = 0;                  // 0x000306e0
    virtual void StopKeyTimer() = 0;                         // 0x00030768
    virtual int GetActiveKeyCode() const = 0;                // VKeyDriver (0x000aa64c) +0x10
    virtual bool IsMultiHardkeyPermit() const = 0;           // 0x00062d50
    // VControlTask 0x00032a60 -> SigKeyEventNotify(key, state).
    virtual void NotifyDisplayKey(uint8_t key, uint8_t state) = 0;
    virtual void NotifyBapKey(uint8_t key, uint8_t state) = 0;  // VBAPProxy 0x000b014c
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_IFILTERACTIONS_H
