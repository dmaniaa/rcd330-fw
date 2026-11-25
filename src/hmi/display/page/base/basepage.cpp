// Stock source: display/page/base/basepage.cpp
#include "display/page/base/basepage.h"

#include <QtCore/QMetaType>

#include <sys/types.h>

#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "display/controls/controlbase/igdtbuttonbase.h"
#include "display/controls/controlparts/vmainpagestatusline.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "display/page/base/pagemanager.h"
#include "service/osal/osal_api.h"

using STDPlusLogicalControlLibrary::IgdtButtonBase;
using STDPlusLogicalControlLibrary::ItemContainer;

namespace {

const char kFile[] = "display/page/base/basepage.cpp";

// Status-line nodes (screen-independent tags inside every main screen).
const uint32_t kBranchBapSensor = 0x1000004;  // BAP sensor / phone link icon
const uint32_t kBranchTemperature = 0x1000005;
const uint32_t kBranchBtConnected = 0x1000001;
const uint32_t kBranchBtSignal = 0x1000002;
const uint32_t kBranchBtBattery = 0x1000003;

// Ticks (VDisplayTask 20 ms timer, UpdateHeadline) between clock refreshes.
const uint8_t kHeadlineReload = 0x14;

// Stock globals shared by every page.
uint8_t g_headlineTicks = 0;        // 0x00a0f58b
bool g_headlineFirstTime = true;    // 0x0096c89e

// ERROR "The pointer: %s == NULL" as stock (format string 0x003ec944).
void LogNull(const char* what, const char* func, int line) {
    log_simple("ERROR", " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func, line);
    printlog(3, "ERROR", kFile, func, line, " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func,
             line);
}

} // namespace

// 0x0002a7d0 VBasePage::VBasePage
VBasePage::VBasePage()
    : ComplexControl(0, 0, 800, 600, ItemContainer(), 0, 0), m_active(false), m_statusline(0), m_screen_id(-1) {
    // The page signals use these typedef names (SigClicked(Item*,int32_t,...)).
    qRegisterMetaType<int32_t>("int32_t");
    qRegisterMetaType<u_int32_t>("u_int32_t");
    m_statusline = new VMainPageStatusLine();
}

// 0x0002a8fc VBasePage::~VBasePage
VBasePage::~VBasePage() {
    delete m_statusline;
}

// 0x0002aa98 VBasePage::GetScreenId
int VBasePage::GetScreenId() const {
    return m_screen_id;
}

// Stock 0x0002aaa0 (no decompile export: a single store to +0x64).
void VBasePage::SetScreenId(int scr_id) {
    m_screen_id = scr_id;
}

// 0x0002adf0 VBasePage::Start
void VBasePage::Start() {
    UpdateHeadlineDisplay();
    m_active = true;
    if (GetScreenId() < 0) return;
    hmi_wrapper_igdt_insert_page((uint8_t)GetScreenId(), 0, this);
}

// DEVIATION: stock stops every child through its Stop slot (0x002c7e94);
// ours only stops the button press timers, the one child state that
// outlives the page's screen.
// 0x0002aaa8 VBasePage::Stop
void VBasePage::Stop() {
    const ItemContainer& children = GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        IgdtButtonBase* button = dynamic_cast<IgdtButtonBase*>(children[i]);
        if (button) button->StopPressTimers();
    }
    if (GetScreenId() >= 0) {
        hmi_wrapper_igdt_remove_page((uint8_t)GetScreenId(), 0);
    }
    m_active = false;
}

// DEVIATION: as Stop, the children pause (0x002c7ec8) is reduced to the
// button press timers.
// 0x0002aaf4 VBasePage::Pause
void VBasePage::Pause() {
    const ItemContainer& children = GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        IgdtButtonBase* button = dynamic_cast<IgdtButtonBase*>(children[i]);
        if (button) button->StopPressTimers();
    }
    m_active = false;
}

// 0x0002ab0c VBasePage::Resume
void VBasePage::Resume() {
    m_active = true;
}

// 0x0002ab58 VBasePage::OnTouchEvent
void VBasePage::OnTouchEvent(int32_t x, int32_t y, int raw_type) {
    (void)x;
    (void)y;
    if (!m_active) {
        const bool reachable = GetScreenId() >= 0 &&
                               VPageManager::Instance()->IsScreenInStack((uint8_t)GetScreenId()) &&
                               VDisplayProxy::Instance()->IsPopupStackClear();
        if (!reachable && raw_type != 5) return;
    }
    // TODO(phase3): the status-line exclusion rects (DAT_00a0f54d..51) and
    // the raw children walk (0x002c7b3c); no phase-2 page has raw children.
}

// 0x0002acd0 VBasePage::DispatchIgdtTouch
bool VBasePage::DispatchIgdtTouch(uint32_t tag, const IgdtTouchEvent& ev) {
    if (!m_active) {
        if (GetScreenId() < 0) return false;
        if (!VPageManager::Instance()->IsScreenInStack((uint8_t)GetScreenId())) return false;
        if (!VDisplayProxy::Instance()->IsPopupStackClear()) return false;
    }
    // 0x002c7c40: the first child that takes the touch ends the walk.
    IgdtTouchEvent copy = ev;
    const ItemContainer& children = GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        IgdtButtonBase* button = dynamic_cast<IgdtButtonBase*>(children[i]);
        if (button && button->IgdtTouchScreenHandler(tag, &copy)) return true;
    }
    return false;
}

// Reduced port: stock also drives the BAP sensor / phone-link icon
// (BAP 0x000b691c, phone 0x001fcea4), the mute icon (VAudioProxy 0x0004729c),
// the outside temperature (platform 2 only) and the BT signal / battery
// icons (VBTProxy 0x000c2c00..). Phase 2 has none of those proxies, so the
// status line shows the stock values for "no BAP, no phone, no BT".
// 0x0002ad60 VBasePage::UpdateHeadlineDisplay
void VBasePage::UpdateHeadlineDisplay() {
    if (!m_statusline) {
        LogNull("m_statusline", "UpdateHeadlineDisplay", 0x165);
        return;
    }
    // TODO(phase3): BAP sensor 1 + phone link (0x001fcea4) set branch 1..5
    // and text 7; without them stock takes the branch-0 path.
    hmi_wrapper_igdt_set_branch(kBranchBapSensor, 0);
    // TODO(phase3): on the first call, the mute icon 0x1000000 from
    // VAudioProxy 0x0004729c unless audio mode 9, and the BT state cache.
    if (g_headlineTicks == 0) {
        g_headlineTicks = kHeadlineReload;
        // TODO(phase3): platform 2 shows the BAP outside temperature in text 8.
        hmi_wrapper_igdt_set_branch(kBranchTemperature, 0);
        m_statusline->setTitleBarTime();
    }
    --g_headlineTicks;
    // TODO(phase3): every third call (DAT_0096c8a4) the mute icon refreshes.
    if (!g_headlineFirstTime) return;
    g_headlineFirstTime = false;
    // No BT phone connected (VBTProxy 0x000c2c00 / 0x000c32d0 == 0).
    hmi_wrapper_igdt_set_branch(kBranchBtConnected, 1);
    hmi_wrapper_igdt_set_branch(kBranchBtSignal, 4);
    hmi_wrapper_igdt_set_branch(kBranchBtBattery, 1);
}
