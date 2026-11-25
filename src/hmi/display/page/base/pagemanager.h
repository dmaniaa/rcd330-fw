// Stock source: display/page/base/pagemanager.cpp (printlog file names).
//
// VPageManager (singleton 0x009704e0, 0x140 bytes): the page stack. Each
// IGDT layer (0..28, scr_table +0 of the page's screen) holds at most one
// page entry {page, screen, active}; m_top is the highest layer in use.
// Inserting above the top pauses the old top page; removing the top page
// resumes the next active page below. A removed page is deleted on the
// next removal (stock +0x134), never while it may still be on the stack.
//
// Threads: the mutators and touch / headline fan-out run on the
// VDisplayTask thread (OnShowPage & co. are queued there). VControlTask
// reads the stack through IPageQuery from its own thread, so every table
// access holds a recursive QMutex (page Start / Stop re-enter
// InsertPage / RemovePage through the IGDT wrapper). DEVIATION: stock has
// no lock.
#ifndef RCD330_HMI_DISPLAY_PAGE_BASE_PAGEMANAGER_H
#define RCD330_HMI_DISPLAY_PAGE_BASE_PAGEMANAGER_H

#include <QtCore/QMutex>
#include <QtCore/QObject>

#include <stdint.h>

#include "control_logic/vcontroltask.h"
#include "display/igdt/igdt_touch.h"

class VBasePage;

class VPageManager : public QObject, public IPageQuery {
    Q_OBJECT
public:
    static VPageManager* Instance();
    virtual ~VPageManager();

    // Creates (or re-shows) the page of scr_id and starts it. Screens
    // without a phase-2 page are logged once and ignored.
    void ShowPage(uint8_t scr_id);
    // ShowPage, then stops every page above scr_id's layer (popups stay).
    void ForceShowPage(uint8_t scr_id);
    // Stops the page of scr_id (its Stop removes it from the stack).
    void ClosePage(uint8_t scr_id);
    // Stops every active page, top first.
    void RemoveAllPage();

    // hmi_wrapper_igdt_insert_page / remove_page: put page on `layer` as
    // scr_id and show the screen / take scr_id's entry off the stack.
    bool InsertPage(uint8_t scr_id, int mode, uint8_t layer, VBasePage* page);
    bool RemovePage(uint8_t scr_id);
    VBasePage* FindPage(uint8_t scr_id) const;

    // Touch fan-out, top layer first: raw tslib events reach every active
    // page; an IGDT node touch stops at the first page that consumes it.
    void OnTouchEvent(int32_t x, int32_t y, int raw_type);
    void OnIgdtTouchEvent(uint32_t tag, const IgdtTouchEvent& ev);
    // Display tick: UpdateHeadlineDisplay on every active page.
    void UpdateHeadline(int ticks);

    // IPageQuery
    virtual int GetCurrentScreen() const;
    virtual int GetBaseScreen() const;
    virtual bool IsScreenShown(uint8_t scr_id) const;
    // True when scr_id is on the stack, every active page above it is a
    // popup, and the top page is a popup.
    virtual bool IsScreenInStack(uint8_t scr_id) const;

private:
    struct Entry {
        VBasePage* page;  // +0
        uint8_t scr_id;   // +4, 0xff = empty
        bool active;      // +5
    };
    enum { kLayerCount = 29 };

    VPageManager();
    void ShowIgdtScreen(uint8_t scr_id);
    void RemovePagesAbove(uint8_t layer);
    int FindEntry(uint8_t scr_id) const;

    Entry m_layers[kLayerCount];    // +0x08
    uint8_t m_top;                  // +0xf0
    uint8_t m_inserting;            // +0xf1: layer being inserted (0 = none)
    bool m_force_show;              // +0xf2
    VBasePage* m_deferred_delete;   // +0x134
    Entry m_saved;                  // +0x138: replaced top entry, stopped after the insert
    mutable QMutex m_mutex;
};

#endif
