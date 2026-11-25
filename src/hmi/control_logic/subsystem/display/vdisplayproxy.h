// Stock source: control_logic/subsystem/display/vdisplayproxy.cpp (printlog
// in 0x00054fcc).
//
// Logical page / popup bookkeeping between the control logic and the display
// task. Page changes leave through IDisplaySink, which VDisplayTask
// implements (bound in Task 9), so this proxy does not depend on VDisplayTask.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_DISPLAY_VDISPLAYPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_DISPLAY_VDISPLAYPROXY_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

// The three VDisplayTask signals stock VDisplayProxy emits directly
// (moc metaobject 0x008e3ed8): index 13 SigShowPage (0x003afa64), 14
// SigForceShowPage (0x003afa9c) and 15 SigClosePage (0x003afad4). Arguments
// are IGDT screen ids.
class IDisplaySink {
public:
    virtual ~IDisplaySink() {}
    virtual void SigShowPage(uint8_t scr_id) = 0;
    virtual void SigForceShowPage(uint8_t scr_id) = 0;
    virtual void SigClosePage(uint8_t scr_id) = 0;
};

class VDisplayProxy : public VBaseProxy {
public:
    enum { kPopPageCount = 0xb4 };
    // Popup slot states (+0x14 array); 1 = shown, 2 = closed.
    enum { kPopShown = 1, kPopClosed = 2 };

    static VDisplayProxy* Instance();

    void Init();
    // NULL drops page changes (logged) until VDisplayTask binds itself.
    void SetDisplaySink(IDisplaySink* sink);

    // Main page for an audio source; force = 1 always switches.
    void SetMainPage(int audio_src, int force);
    // Shows a logical page (1 = Tuner, 9 = App-Connect, ...).
    void InsertPage(int page_id);
    // Pushes an IGDT screen on top of the current page.
    void PushPage(uint8_t scr_id);
    // Closes popup slot pop_id unless its auto-close timer is still running.
    void ClosePage(uint8_t pop_id);
    // Closes popup slot pop_id now.
    void ForceClosePage(uint8_t pop_id);
    // Removes an IGDT screen.
    void RemovePage(uint8_t scr_id);
    int GetLanguage() const;
    int GetRegion() const;
    // Stores the region and switches the language it implies (or keeps it).
    void SetRegion(int region);
    // Stores the language and switches IGDT to it.
    void SetLanguage(int lang);
    // SetRegion, then the PSS setup write (TODO(phase3)).
    void ApplyRegion(int region);

    int GetMainPage() const;
    int GetCurDisplayPage() const;
    void SetMainPageId(int page_id);
    int GetPopPageState(uint32_t pop_id) const;
    void NotifyHmiOn();
    void UpdateBacklight();
    // Raw +0x2c / +0x30 stores (VControlTask 0x74:0x81 path); unlike
    // SetMainPageId they have no side effects.
    void SetMainPageValue(int page_id);
    void SetPrevMainPageValue(int page_id);
    // +0xc8: set by the first hmi_wrapper_igdt_show; hard keys are dropped
    // until then (VBaseFilter::HandleLPCLHardKey).
    void SetIgdtShown(uint8_t shown);
    uint8_t IsIgdtShown() const;
    // True when scr_id (non-zero) is the screen of a popup slot.
    bool IsPopupScreen(uint8_t scr_id) const;
    // False while an entry of the popup stack is up (pages ignore touches
    // under a popup then).
    bool IsPopupStackClear() const;

private:
    VDisplayProxy();
    void ClosePopSlot(uint8_t pop_id);
    void EmitForceShow(uint8_t scr_id);

    IDisplaySink* m_sink;
    int m_state;              // +0x04: 1 = constructed, 2 = Init done
    int m_cur_display_page;   // +0x0c
    int m_main_page;          // +0x2c
    int m_prev_main_page;     // +0x30
    uint8_t m_popup_owner;    // +0x35
    int m_region;             // +0x44
    int m_language;           // +0x48
    int m_backlight_timer;    // +0x50
    int m_pending_popup;      // +0xd4
    volatile uint8_t m_igdt_shown;  // +0xc8 (written on the igdtTask thread)
    bool m_logged_phase3_page;  // ours: InsertPage log-once for phase-3 pages
    bool m_logged_page_fallback;  // ours: InsertPage log-once for the Tuner fallback
    int m_pop_state[kPopPageCount];    // +0x14 -> array
    int m_pop_timer[kPopPageCount];    // +0x1c -> array
    uint8_t m_pop_pending[kPopPageCount];  // +0x20 -> array
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_DISPLAY_VDISPLAYPROXY_H
