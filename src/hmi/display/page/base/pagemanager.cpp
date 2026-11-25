// Stock source: display/page/base/pagemanager.cpp
#include "display/page/base/pagemanager.h"

#include <QtCore/QMutexLocker>

#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "control_logic/subsystem/proxy_decoders.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "display/igdt/igdt_runtime.h"
#include "display/page/appconnect/vappconnectinfo.h"
#include "display/page/appconnect/vappnoconnection.h"
#include "display/page/base/basepage.h"
#include "display/page/tuner/vtunermainpage.h"
#include "service/osal/osal_api.h"

namespace {

const char kFile[] = "display/page/base/pagemanager.cpp";
const uint8_t kNoScreen = 0xff;

// Screens ShowPage builds in phase 2 (kPhase2PageScreens lists them; the
// switch maps each to its page class).
const uint8_t kScrAppConnectInfo = 0x03;
const uint8_t kScrAppNoConnection = 0x04;
const uint8_t kScrClimatePopup = 0x1f;   // popup closed when a main page shows
const uint8_t kScrUnsupportedPopup = 0x26;
const uint8_t kScrKeepOnForce = 0x6c;    // RemovePagesAbove leaves it
const uint8_t kScrTuner = 0xf9;

VPageManager* g_pageManager = 0;    // 0x009704e0
bool g_phase3Logged[256];           // ours: ShowPage log-once per missing page

} // namespace

// 0x0002b2a4 VPageManager::VPageManager
VPageManager::VPageManager()
    : QObject(0), m_top(0), m_inserting(0), m_force_show(false), m_deferred_delete(0), m_mutex(QMutex::Recursive) {
    for (int i = 0; i < kLayerCount; ++i) {
        m_layers[i].page = 0;
        m_layers[i].scr_id = kNoScreen;
        m_layers[i].active = false;
    }
    m_saved.page = 0;
    m_saved.scr_id = kNoScreen;
    m_saved.active = false;
    // TODO(phase3): VControlTask SigRemoveHighestPage -> RemoveHighestPriorityPage
    // and the phone / climate page-stack fields (+0xf4..+0x131).
}

VPageManager::~VPageManager() {}

// 0x0002b398 VPageManager::Instance
VPageManager* VPageManager::Instance() {
    if (!g_pageManager) {
        g_pageManager = new VPageManager();
    }
    return g_pageManager;
}

// Index of the first active entry showing scr_id, else -1. Name is ours
// (the lookup loop of 0x0002b8b8 / 0x0002b428 / 0x0002b4ac).
int VPageManager::FindEntry(uint8_t scr_id) const {
    for (int i = 0; i < kLayerCount; ++i) {
        if (m_layers[i].active && m_layers[i].scr_id == scr_id) return i;
    }
    return -1;
}

// 0x0002b8b8 VPageManager::FindPage
VBasePage* VPageManager::FindPage(uint8_t scr_id) const {
    QMutexLocker lock(&m_mutex);
    const int i = FindEntry(scr_id);
    return i < 0 ? 0 : m_layers[i].page;
}

// IPageQuery: FindPage (0x0002b8b8) != NULL.
bool VPageManager::IsScreenShown(uint8_t scr_id) const {
    return FindPage(scr_id) != 0;
}

// 0x0002bb60 VPageManager::GetCurrentScreen
int VPageManager::GetCurrentScreen() const {
    QMutexLocker lock(&m_mutex);
    return m_layers[m_top].scr_id;
}

// 0x0002bb70 VPageManager::GetBaseScreen
int VPageManager::GetBaseScreen() const {
    QMutexLocker lock(&m_mutex);
    return m_layers[0].scr_id;
}

// 0x0002bbf8 VPageManager::IsScreenInStack
bool VPageManager::IsScreenInStack(uint8_t scr_id) const {
    QMutexLocker lock(&m_mutex);
    const VDisplayProxy* display = VDisplayProxy::Instance();
    if (!display->IsPopupScreen(m_layers[m_top].scr_id)) return false;
    if (m_top == 0) return false;
    int i = 0;
    while (i < m_top && !(m_layers[i].scr_id == scr_id && m_layers[i].active)) ++i;
    if (i == m_top) return false;
    int j = i + 1;
    while (j < m_top && !(m_layers[j].active && !display->IsPopupScreen(m_layers[j].scr_id))) ++j;
    return j == m_top;
}

// 0x0002c34c VPageManager::ShowPage
void VPageManager::ShowPage(uint8_t scr_id) {
    log_simple("INFO", " ==== HMI ==== INFO: ##########################VPageManager::ShowPage %d", scr_id);
    printlog(3, "INFO", kFile, "OnShowPage", 0x89,
             " ==== HMI ==== INFO: ##########################VPageManager::ShowPage %d", scr_id);
    VDisplayProxy* display = VDisplayProxy::Instance();
    // 0x0002beb4: a non-popup page closes the climate popup.
    if (display->GetPopPageState(kScrClimatePopup) == 1 && scr_id != 0 && !display->IsPopupScreen(scr_id)) {
        display->ForceClosePage(kScrClimatePopup);
    }

    QMutexLocker lock(&m_mutex);
    if (!ScreenHasPhase2Page(scr_id)) {
        // TODO(phase3): the other ~120 pages of the stock switch.
        if (!g_phase3Logged[scr_id]) {
            g_phase3Logged[scr_id] = true;
            log_simple("INFO", " ==== HMI ==== INFO: Phase-3 page %d not yet implemented", scr_id);
        }
        return;
    }
    VBasePage* page = 0;
    bool reuse = false;
    switch (scr_id) {
    case kScrAppConnectInfo:
        page = new vAppConnectInfo();
        break;
    case kScrAppNoConnection:
        // STOCK-QUIRK: any entry with screen 4, active or not, is reused.
        for (int i = 0; i < kLayerCount; ++i) {
            if (m_layers[i].scr_id == kScrAppNoConnection) reuse = true;
        }
        if (!reuse) page = new vAppNoConnection();
        break;
    case kScrTuner:
        page = new VTunerMainPage();
        break;
    default:
        // A screen in kPhase2PageScreens without a case here is a bug.
        log_simple("ERROR", " ==== HMI ==== ERROR: VPageManager::ShowPage: no page class for phase-2 screen %d",
                   scr_id);
        return;
    }

    VBasePage* existing = FindPage(scr_id);
    if (reuse && existing) {
        if (!hmi_wrapper_igdt_is_page_shown(scr_id)) ShowIgdtScreen(scr_id);
        return;
    }
    if (page) page->Start();
}

// 0x0002fc28 VPageManager::ForceShowPage
void VPageManager::ForceShowPage(uint8_t scr_id) {
    QMutexLocker lock(&m_mutex);
    m_force_show = true;
    const uint8_t layer = IgdtScreenLayer(g_igdtTablesVw, scr_id);
    if (scr_id == kScrTuner && layer < kLayerCount && m_layers[layer].scr_id == kScrTuner) {
        // The tuner is still on its layer: keep the page, only re-show it.
        if (FindPage(kScrTuner)) {
            // TODO(phase3): tuner refresh 0x0011e280(page, 0) / 0x0011e434(page).
            if (!hmi_wrapper_igdt_is_page_shown(kScrTuner)) ShowIgdtScreen(kScrTuner);
        }
    } else {
        // TODO(phase3): screen 99 re-shows like the tuner (0x00046b38 / 0x000475e8).
        ShowPage(scr_id);
    }
    RemovePagesAbove(layer);
    m_force_show = false;
}

// 0x0002b7cc VPageManager::RemovePagesAbove
void VPageManager::RemovePagesAbove(uint8_t layer) {
    QMutexLocker lock(&m_mutex);
    if (m_top <= layer) return;
    const VDisplayProxy* display = VDisplayProxy::Instance();
    uint8_t new_top = layer;
    for (unsigned i = layer + 1u; i <= m_top; ++i) {
        const Entry& e = m_layers[i];
        // Popups (and screen 0x6c) stay above the shown page.
        if (display->IsPopupScreen(e.scr_id) || e.scr_id == kScrKeepOnForce) {
            new_top = (uint8_t)i;
            continue;
        }
        // TODO(phase3): with an App-Connect session (0x00051b74 == 1) in
        // region 2, screens 0xe8 / 0xda / 0xd9 / 0xdb stay as well.
        if (e.active && e.page) e.page->Stop();
    }
    m_top = new_top;
}

// 0x0002b428 VPageManager::ClosePage
void VPageManager::ClosePage(uint8_t scr_id) {
    VBasePage* page = FindPage(scr_id);
    if (page) {
        page->Stop();
        return;
    }
    if (scr_id == kScrUnsupportedPopup) {
        // TODO(phase3): VDisplayProxy 0x000516f8(300).
    }
}

// 0x0002b64c VPageManager::RemoveAllPage
void VPageManager::RemoveAllPage() {
    QMutexLocker lock(&m_mutex);
    if (m_top >= kLayerCount) return;
    for (int i = m_top; i >= 0; --i) {
        if (m_layers[i].active && m_layers[i].page) m_layers[i].page->Stop();
    }
    m_top = 0;
}

// 0x0002c070 VPageManager::ShowIgdtScreen
void VPageManager::ShowIgdtScreen(uint8_t scr_id) {
    VPowerProxy* power = VPowerProxy::Instance();
    const uint8_t state = power->GetPowerState();
    if (scr_id == kScrTuner || scr_id == 0x52 || scr_id == 10 || scr_id == 0xee || scr_id == 99) {
        // Main screens show in the run states, under popups, or after a
        // phone / remote wake-up.
        const bool run = state == 3 || state == 6 || state == 0x0b || state == 0x0c;
        if (!run && !IsScreenInStack(scr_id)) {
            // TODO(phase3): with popups queued (VDisplayProxy +0x54,
            // 0x00050eb8) the screen shows unless popup 0x0e is up; phase 2
            // never queues one.
            if (power->GetRemoteWakeFlag() == 0 && power->GetPhoneWakeFlag() == 0) return;
        }
    } else if (scr_id == 0x5c) {
        if (state != 7) return;
    } else if (scr_id == kScrUnsupportedPopup) {
        if (state != 7 && state != 2) return;
    }
    hmi_wrapper_igdt_show_screen(scr_id);
}

// 0x0002c1b4 VPageManager::InsertPage
bool VPageManager::InsertPage(uint8_t scr_id, int mode, uint8_t layer, VBasePage* page) {
    (void)mode;
    if (!page || layer >= kLayerCount) return false;
    QMutexLocker lock(&m_mutex);
    page->SetScreenId(scr_id);
    // TODO(phase3): App-Connect base-screen notice (0x0034860c -> 0x00347e10
    // before, 0x00347f30 after).
    m_inserting = layer;
    Entry& e = m_layers[layer];
    VBasePage* old = e.page;
    bool stop_old_after = false;
    if (old) {
        if (layer < m_top || e.scr_id == scr_id) {
            if (old != page) old->Stop();
        } else if (layer == m_top) {
            // The old top page is stopped once the new one is shown.
            m_saved = e;
            stop_old_after = true;
        } else {
            stop_old_after = true;
        }
    }
    m_inserting = 0;
    e.page = page;
    e.scr_id = scr_id;
    e.active = true;
    if (m_top < layer) {
        if (m_layers[m_top].page) m_layers[m_top].page->Pause();
        m_top = layer;
    } else if (layer < m_top) {
        page->SetActive(false);
    }
    // TODO(phase3): 0x000a610c(0x000a6278(), top screen): key focus screen.
    ShowIgdtScreen(scr_id);
    if (stop_old_after) old->Stop();
    return true;
}

// 0x0002b4ac VPageManager::RemovePage
bool VPageManager::RemovePage(uint8_t scr_id) {
    QMutexLocker lock(&m_mutex);
    if (m_top >= kLayerCount) return false;
    Entry* target = 0;
    if (m_layers[m_top].scr_id == scr_id) {
        target = &m_layers[m_top];
        if (m_inserting == 0 && m_top != 0) {
            // Resume the next active page below the removed top.
            int below = m_top - 1;
            while (below >= 0 && !m_layers[below].active) --below;
            if (below >= 0 && m_layers[below].page) {
                m_layers[below].page->Resume();
                m_top = (uint8_t)below;
            } else {
                m_top = 0;
            }
        }
    } else if (m_saved.scr_id == scr_id) {
        target = &m_saved;
    } else {
        const int i = FindEntry(scr_id);
        if (i < 0) return false;
        target = &m_layers[i];
    }
    if (!target->page || !target->active || target->scr_id != scr_id) return false;
    target->active = false;
    delete m_deferred_delete;
    m_deferred_delete = target->page;
    target->page = 0;
    target->scr_id = kNoScreen;
    return true;
}

// 0x0002ba34 VPageManager::OnTouchEvent
void VPageManager::OnTouchEvent(int32_t x, int32_t y, int raw_type) {
    QMutexLocker lock(&m_mutex);
    if (m_top >= kLayerCount) return;
    for (int i = m_top; i >= 0; --i) {
        if (m_layers[i].active && m_layers[i].page) m_layers[i].page->OnTouchEvent(x, y, raw_type);
    }
}

// 0x0002b6b0 VPageManager::OnIgdtTouchEvent
void VPageManager::OnIgdtTouchEvent(uint32_t tag, const IgdtTouchEvent& ev) {
    QMutexLocker lock(&m_mutex);
    if (m_top >= kLayerCount) return;
    for (int i = m_top; i >= 0; --i) {
        VBasePage* page = m_layers[i].page;
        if (m_layers[i].active && page && page->DispatchIgdtTouch(tag, ev)) return;
    }
}

// 0x0002bb00 VPageManager::UpdateHeadline
void VPageManager::UpdateHeadline(int ticks) {
    (void)ticks;
    QMutexLocker lock(&m_mutex);
    if (m_top >= kLayerCount) return;
    for (int i = m_top; i >= 0; --i) {
        if (m_layers[i].active && m_layers[i].page) m_layers[i].page->UpdateHeadlineDisplay();
    }
}
