// Stock source: control_logic/subsystem/display/vdisplayproxy.cpp
//
// Phase 2 keeps the page-id plumbing (audio source -> logical page -> IGDT
// screen) and the popup slot states. The page-history stack, the popup
// manager (ShowPopPage 0x00053d94) and the per-page exceptions for phone,
// MirrorLink, CarPlay and Android Auto are TODO(phase3) where stock has them.
#include "control_logic/subsystem/display/vdisplayproxy.h"

#include <string.h>

#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/bap/vbapproxy.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "control_logic/subsystem/proxy_decoders.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "service/osal/osal_api.h"

#define DISPLAY_LOG(func, line, ...)                                                                       \
    do {                                                                                                   \
        log_simple("INFO", __VA_ARGS__);                                                                   \
        printlog(3, "INFO", "control_logic/subsystem/display/vdisplayproxy.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

// 0x003f3950 kPopPageScreen
// IGDT screen id per popup slot.
const uint8_t kPopPageScreen[VDisplayProxy::kPopPageCount] = {
    0x00, 0x00, 0x00, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x9f, 0x1a, 0x1a, 0x1a, 0xdf, 0xa2, 0x00, 0xd8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x51, 0x77, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0xb5, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x8f, 0x90, 0x91, 0x92, 0xdc, 0xb7, 0xb8, 0xba, 0xbb, 0xb9, 0xc5, 0x81, 0x82, 0x83, 0x84, 0x85,
    0x86, 0x00, 0x99, 0x87, 0x88, 0x9a, 0x89, 0x9b, 0x8a, 0x8b, 0x8c, 0x9d, 0x8e, 0x75, 0xa9, 0xaa, 0xac, 0x76,
    0xad, 0xae, 0xa7, 0xa8, 0xbc, 0xaf, 0xc0, 0x79, 0xc1, 0x8d, 0xc3, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd7,
    0x9e, 0xc2, 0x9c, 0xab, 0x00, 0xc9, 0x00, 0x51, 0x51, 0x51, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x98, 0xbe,
    0xbf, 0x7e, 0xca, 0xdb, 0xda, 0xd9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdd, 0x00, 0x00, 0x00, 0x00, 0x7b,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xde, 0xde, 0xc7, 0x00, 0x80, 0xa5, 0xb0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xa0, 0x51, 0x51, 0x00, 0xcd, 0x70, 0x72, 0x6e, 0x7d, 0x71, 0x72, 0x73, 0xce, 0xbd,
    0xa4, 0xa6, 0x7a, 0xd5, 0xb1, 0xc6, 0xe8, 0x0d, 0xc4, 0x78, 0x74, 0xb2, 0xd6, 0xb3, 0xa3, 0xb4, 0xb6, 0x6f,
};

const int kPageTuner = 1;
const int kPageMirrorLink = 10;
const int kNoRecoverPage = 0xff;

VDisplayProxy* g_displayProxy = 0;

// 0x00970560 g_lastMainPage
// Last logical main page (1..10) InsertPage showed. Name is ours.
int g_lastMainPage = 0;

// Pages InsertPage shows even while the HMI is off (popups, RVC, standby).
bool InsertAllowedWhileOff(int page_id) {
    return page_id == 0x13 || page_id == 0x12 || page_id == 0x0c || page_id == 0x0e || page_id == 8 ||
           page_id == 7 || page_id == 9;
}

} // namespace

// 0x00051e8c VDisplayProxy::VDisplayProxy
VDisplayProxy::VDisplayProxy()
    : m_sink(0),
      m_state(1),
      m_cur_display_page(0),
      m_main_page(0),
      m_prev_main_page(0),
      m_popup_owner(0),
      m_region(0),
      m_language(0),
      m_backlight_timer(0),
      m_pending_popup(0),
      m_igdt_shown(0),
      m_logged_phase3_page(false),
      m_logged_page_fallback(false) {
    // TODO(phase3): popup timer queue (+0x08), popup table copy from
    // 0x00967a74, the 10-entry popup stack (+0x64) and the page history.
    for (int i = 0; i < kPopPageCount; ++i) {
        m_pop_state[i] = 0;
        m_pop_timer[i] = 0;
    }
    memset(m_pop_pending, 0, sizeof(m_pop_pending));
}

// 0x00052174 VDisplayProxy::Instance
VDisplayProxy* VDisplayProxy::Instance() {
    if (g_displayProxy == 0) {
        g_displayProxy = new VDisplayProxy();
    }
    return g_displayProxy;
}

// 0x000531a8 VDisplayProxy::Init
void VDisplayProxy::Init() {
    m_state = 2;
    // TODO(phase3): popup timer setup (0x00037680, 0x00038248 x10), the
    // pop-page vectors (+0xc4, +0xcc) and 0x001cd104.
    m_popup_owner = 0;
    m_language = 1;
    m_pending_popup = kPopPageCount;
    m_igdt_shown = 0;
}

void VDisplayProxy::SetDisplaySink(IDisplaySink* sink) {
    m_sink = sink;
}

void VDisplayProxy::EmitForceShow(uint8_t scr_id) {
    if (m_sink == 0) {
        log_simple("INFO", " ==== HMI ==== INFO: VDisplayProxy: no display sink, screen 0x%02x not shown\n", scr_id);
        return;
    }
    m_sink->SigForceShowPage(scr_id);
}

// 0x00054fcc VDisplayProxy::SetMainPage
void VDisplayProxy::SetMainPage(int audio_src, int force) {
    DISPLAY_LOG("SetMainPage", 0x375, " ==== HMI ==== INFO: VDisplayProxy::SetMainPage %d %d", audio_src, force);
    const int page = MainPageForAudioSource(audio_src);
    if (force != 1) {
        // TODO(phase3): without force stock keeps phone, MirrorLink, CarPlay,
        // Android Auto, setup and popup pages up (0x00055060..0x00055230);
        // with none of those in phase 2 it takes the force path.
    }
    SetMainPageId(page);
    if (VPowerProxy::Instance()->GetRecoverPage() == kNoRecoverPage) {
        InsertPage(page);
    }
}

// 0x00054314 VDisplayProxy::InsertPage
void VDisplayProxy::InsertPage(int page_id) {
    // TODO(phase3): phone page (4) in power state 7 closes the call popups.
    if (m_state == 1) {
        return;  // before Init
    }
    VPowerProxy* power = VPowerProxy::Instance();
    if (power->GetHmiRunFlag() == 0 && !InsertAllowedWhileOff(page_id)) {
        if (power->GetPhoneWakeFlag() == 0 && power->GetRemoteWakeFlag() == 0) {
            // TODO(phase3): stock also needs the page manager to have no page
            // up (0x0002bb70 == 0xff).
            if (g_lastMainPage != 0) {
                return;
            }
        }
    }
    DISPLAY_LOG("InsertPage", 0x5bd,
                " ==== HMI ==== INFO: VDisplayProxy::InsertPage %d,m_cur_display_page %d, cur highest page = %d",
                page_id, m_cur_display_page, 0xff);
    // TODO(phase3): CarPlay / Android Auto page notifications (0x0032e158,
    // 0x00384db0), MirrorLink (10) and phone (4) page variants, and closing
    // the phone page when leaving it (0x000527e8).
    int show_page = page_id;
    const Phase2PageAction action = ResolvePhase2Page(page_id, g_lastMainPage, &show_page);
    if (action == kPhase2PageIgnore) {
        // Stock 0x00054314 takes a separate branch for MirrorLink (10): it
        // checks screens 0x54..0x57, logs DISPLAY_PAGE_MIRRORLINK and never
        // emits table[10] (= 0).
        // DEVIATION: stock then records page 10 at LAB_000544a4; phase 2 has
        // no MirrorLink page, so it changes nothing (log once).
        // TODO(phase3): port the MirrorLink branch (0x001af674 / 0x0002b8b8).
        // DEVIATION: the same holds for every other page whose screen
        // VPageManager does not build in phase 2 (kPhase2PageScreens, e.g.
        // 2 -> 0x52 media, 8 -> 0x02 App-Connect menu) once a main page is
        // up: force-showing it would only drop the pages above its layer and
        // record a page that is not on screen.
        if (!m_logged_phase3_page) {
            log_simple("INFO", " ==== HMI ==== INFO: VDisplayProxy::InsertPage: page %d not shown in phase 2\n", page_id);
            m_logged_phase3_page = true;
        }
        return;
    }
    if (action == kPhase2PageFallBack) {
        // DEVIATION: a main page without a phase-2 screen while no main page
        // is up (boot with media / BT as the last source: the 0x81 audio mode
        // makes page 2 the main page) shows the Tuner shell (the stock boot
        // page) instead; a blank screen until RADIO / APP is worse.
        if (!m_logged_page_fallback) {
            log_simple("INFO", " ==== HMI ==== INFO: VDisplayProxy::InsertPage: page %d not in phase 2, showing page %d\n",
                       page_id, show_page);
            m_logged_page_fallback = true;
        }
        page_id = show_page;
    }
    const uint8_t scr = ScreenForPage(page_id);
    if (page_id == 8 || page_id == 9) {
        // TODO(phase3): in power state 7 these pages are pushed only when no
        // standby clock is up (0x00054470).
    }
    EmitForceShow(scr);
    if (page_id == kPageTuner) {
        // TODO(phase3): tuner traffic popup 0x78 (0x000af304).
    }
    // TODO(phase3): page history push (0x00052ad4) and VPowerProxy +0x15.
    m_cur_display_page = page_id;
    if (page_id >= 1 && page_id <= 10) {
        g_lastMainPage = page_id;
        if (page_id != m_main_page) {
            SetMainPageId(page_id);
        }
    }
    // TODO(phase3): CarLife page notification (0x0036bb5c).
}

// 0x000530e4 VDisplayProxy::PushPage
void VDisplayProxy::PushPage(uint8_t scr_id) {
    // TODO(phase3): region-2 units with the BAP flag (+0xe0) turn screen
    // 0x1f into a BAP message instead (0x000b4ecc).
    if (m_sink == 0) {
        log_simple("INFO", " ==== HMI ==== INFO: VDisplayProxy: no display sink, screen 0x%02x not pushed\n", scr_id);
    } else {
        m_sink->SigShowPage(scr_id);
    }
    if (scr_id >= 0x54 && scr_id <= 0x57) {
        // MirrorLink screens make MirrorLink the main page.
        // TODO(phase3): page history push (0x00052ad4) and OnSaveAudioModeToVIP.
        m_main_page = kPageMirrorLink;
        m_cur_display_page = kPageMirrorLink;
    }
}

void VDisplayProxy::ClosePopSlot(uint8_t pop_id) {
    m_pop_state[pop_id] = kPopClosed;
    m_pop_timer[pop_id] = 0;
    m_pop_pending[pop_id] = 0;
    // TODO(phase3): pop the 10-entry popup stack and re-show the popup under
    // it (0x00051828, 0x00053454), and dim the display when the HMI is off
    // and no popup is left (0x000512d0, 0x00056a00).
    if (m_sink == 0) {
        log_simple("INFO", " ==== HMI ==== INFO: VDisplayProxy: no display sink, popup %u not closed\n", pop_id);
        return;
    }
    m_sink->SigClosePage(kPopPageScreen[pop_id]);
}

// 0x00053b98 VDisplayProxy::ClosePage
void VDisplayProxy::ClosePage(uint8_t pop_id) {
    if (pop_id >= kPopPageCount || m_pop_state[pop_id] != kPopShown) {
        return;
    }
    if (m_pop_timer[pop_id] != 0) {
        m_pop_pending[pop_id] = 1;  // closed when its timer fires
        return;
    }
    // TODO(phase3): RVC popup 0x0e resets the camera (0x0004cfe4).
    ClosePopSlot(pop_id);
}

// 0x000537fc VDisplayProxy::ForceClosePage
// Name is ours.
void VDisplayProxy::ForceClosePage(uint8_t pop_id) {
    if (pop_id >= kPopPageCount || m_pop_state[pop_id] != kPopShown) {
        return;
    }
    ClosePopSlot(pop_id);
}

// 0x000515f8 VDisplayProxy::RemovePage
void VDisplayProxy::RemovePage(uint8_t scr_id) {
    if (m_sink == 0) {
        log_simple("INFO", " ==== HMI ==== INFO: VDisplayProxy: no display sink, screen 0x%02x not removed\n", scr_id);
        return;
    }
    m_sink->SigClosePage(scr_id);
}

// 0x00050ad0 VDisplayProxy::GetLanguage
int VDisplayProxy::GetLanguage() const {
    return m_language;
}

// 0x000508e8 VDisplayProxy::GetRegion
// Name is ours.
int VDisplayProxy::GetRegion() const {
    return m_region;
}

// 0x000509b0 VDisplayProxy::SetRegion
// Name is ours.
void VDisplayProxy::SetRegion(int region) {
    m_region = region;
    int lang = 0;
    if (region < 0 || region > 0xff ||
        !RegionToLanguage(static_cast<uint8_t>(region), VAppConnectProxy::Instance()->GetRegionMode(),
                          VBAPProxy::Instance()->GetVehicleCountry(), &lang)) {
        return;
    }
    SetLanguage(lang);
}

// 0x000508f0 VDisplayProxy::SetLanguage
// Name is ours.
void VDisplayProxy::SetLanguage(int lang) {
    m_language = lang;
    hmi_wrapper_igdt_set_lang(lang & 0xff);
    // TODO(phase3): on platform 2 stock also sends the language's units to
    // the vehicle (0x000b2c00 per language).
    // TODO(phase3): on every platform stock then posts the language change
    // to VDisplayTask (0x000500b8 with {0xe, 2, 1, lang} -> 0x000633a4 ->
    // 0x00065454); not ported.
}

// 0x00050abc VDisplayProxy::ApplyRegion
// Name is ours.
void VDisplayProxy::ApplyRegion(int region) {
    SetRegion(region);
    // TODO(phase3): VPSSProxy 0x001c3b18 writes the setup block back to PSS.
}

// 0x00050598 VDisplayProxy::GetMainPage
int VDisplayProxy::GetMainPage() const {
    return m_main_page;
}

// 0x00050590 VDisplayProxy::GetCurDisplayPage
int VDisplayProxy::GetCurDisplayPage() const {
    return m_cur_display_page;
}

// 0x000505b8 VDisplayProxy::SetMainPageId
// Name is ours.
void VDisplayProxy::SetMainPageId(int page_id) {
    if (page_id != m_main_page) {
        m_prev_main_page = m_main_page;
        m_main_page = page_id;
        // TODO(phase3): leaving MirrorLink resets its state (0x001af954,
        // 0x001af964).
    }
    // TODO(phase3): VAudioProxy::OnSaveAudioModeToVIP (0x00047810).
}

// 0x00050058 VDisplayProxy::GetPopPageState
// Name is ours.
int VDisplayProxy::GetPopPageState(uint32_t pop_id) const {
    if (pop_id >= kPopPageCount) {
        return 0;
    }
    return m_pop_state[pop_id];
}

// 0x000500b4 VDisplayProxy::NotifyHmiOn
// Name is ours; stock body is empty.
void VDisplayProxy::NotifyHmiOn() {}

// 0x00050bcc VDisplayProxy::UpdateBacklight
// Name is ours.
void VDisplayProxy::UpdateBacklight() {
    VPowerProxy* power = VPowerProxy::Instance();
    if (power->GetHmiRunFlag() == 0) {
        m_backlight_timer = 0;
        return;
    }
    if (m_popup_owner == 0) {
        m_backlight_timer = 0;
        if (power->GetRemoteWakeFlag() != 0) {
            power->RestoreBacklight();
        }
        if (power->GetPhoneWakeFlag() == 0) {
            return;
        }
    } else {
        m_backlight_timer = 10;
        // TODO(phase3): stock skips this while the BAP climate popup flag
        // (VBAPProxy +0x63d) is set.
    }
    power->RestoreBacklight();
}

// 0x000505a8 VDisplayProxy::SetMainPageValue
// Name is ours.
void VDisplayProxy::SetMainPageValue(int page_id) {
    m_main_page = page_id;
}

// 0x000505b0 VDisplayProxy::SetPrevMainPageValue
// Name is ours.
void VDisplayProxy::SetPrevMainPageValue(int page_id) {
    m_prev_main_page = page_id;
}

// 0x000517f4 VDisplayProxy::SetIgdtShown
// Name is ours.
void VDisplayProxy::SetIgdtShown(uint8_t shown) {
    m_igdt_shown = shown;
}

// 0x000517fc VDisplayProxy::IsIgdtShown
// Name is ours.
uint8_t VDisplayProxy::IsIgdtShown() const {
    return m_igdt_shown;
}

// 0x000514f4 VDisplayProxy::IsPopupScreen
// Name is ours.
bool VDisplayProxy::IsPopupScreen(uint8_t scr_id) const {
    if (scr_id == 0) {
        return false;
    }
    for (int i = 0; i < kPopPageCount; ++i) {
        if (kPopPageScreen[i] == scr_id) {
            return true;
        }
    }
    return false;
}

// 0x0005149c VDisplayProxy::IsPopupStackClear
// Name is ours. TODO(phase3): stock walks the 10-entry popup stack (+0x64)
// for an entry in state 1 whose popup table row (0x00967a82) is not
// transparent; the stack is not ported, so it is always clear.
bool VDisplayProxy::IsPopupStackClear() const {
    return true;
}
