// Stock source: display/page/base/basepage.cpp (printlog file names).
//
// Base of every HMI page (vtable 0x008e21e8). A page is a full-screen
// ComplexControl on the VDisplayTask thread: VPageManager creates it in
// ShowPage and calls Start(); Start inserts the page's IGDT screen through
// hmi_wrapper_igdt_insert_page, which hands it back to
// VPageManager::InsertPage. Stop removes the screen the same way, and
// VPageManager deletes the page on the next removal.
//
// Virtual slots (stock vtable offsets): Stop 0x9c, Pause 0xa0,
// OnTouchEvent 0xa8, DispatchIgdtTouch 0xac, GetScreenId 0xf0,
// SetScreenId 0xf4, Start 0xf8, Resume 0xfc, OnReceiveMsg 0x100,
// UpdateHeadlineDisplay 0x104.
//
// DEVIATION (from the Task 9 brief, not from stock): there is no
// GetStatusLineScreenId() and no page shows screen 0xfe. In stock the main
// status line (img_main_page_status_line_bg, 800 x 41 at y 0) is part of the
// TunerMain (0xf9, layer 0) and AppNoConn (0x04, layer 1) scenes themselves;
// AppConnectInfo (0x03, layer 2) draws an opaque 800 x 480 background over
// it, so the status line disappears under 0x03 and returns when 0x03 is
// removed. Screen 0xfe (layer 5, mislabelled "StatusBar" in
// resources/scenes) is VSoundVolumeSetup (ctor 0x001b958c), and the stock
// boot / APP serial never logs "VPageManager::ShowPage 254". Pinned by
// test_controls test_status_line_comes_from_the_page_screens.
#ifndef RCD330_HMI_DISPLAY_PAGE_BASE_BASEPAGE_H
#define RCD330_HMI_DISPLAY_PAGE_BASE_BASEPAGE_H

#include <stdint.h>

#include "display/controls/controlbase/complexcontrol.h"
#include "display/igdt/igdt_touch.h"

class VMainPageStatusLine;

class VBasePage : public STDPlusLogicalControlLibrary::ComplexControl {
    Q_OBJECT
public:
    VBasePage();
    virtual ~VBasePage();

    virtual void Start();
    virtual void Stop();
    // Pause: the page is covered by a higher layer (touches only reach it
    // while every page above is a popup). Resume: it is the top page again.
    virtual void Pause();
    virtual void Resume();

    // -1 until VPageManager::InsertPage assigns the page's screen.
    virtual int GetScreenId() const;
    virtual void SetScreenId(int scr_id);

    // Raw (tslib) touch, x / y in screen pixels, raw_type 3 press, 4 move,
    // 5 release. Only pages with raw children use it (phase 3).
    virtual void OnTouchEvent(int32_t x, int32_t y, int raw_type);
    // IGDT touch on node `tag`: offered to each IgdtButtonBase child until
    // one consumes it. Returns true when consumed.
    virtual bool DispatchIgdtTouch(uint32_t tag, const IgdtTouchEvent& ev);

    // Status line (title bar) refresh; VPageManager::UpdateHeadline calls it
    // every display tick for the active pages.
    virtual void UpdateHeadlineDisplay();

    bool IsActive() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

private:
    bool m_active;                      // +0x5c
    VMainPageStatusLine* m_statusline;  // +0x60
    int m_screen_id;                    // +0x64
};

#endif
