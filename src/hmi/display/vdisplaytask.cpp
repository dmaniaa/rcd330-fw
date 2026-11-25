// Stock source: display/vdisplaytask.cpp
#include "display/vdisplaytask.h"

#include <QtCore/QTimer>

#include "control_logic/vcontroltask.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "display/multitouch/vmultitouch.h"
#include "display/page/base/pagemanager.h"

namespace {

const int kDisplayTickMs = 20;      // 0x00063488 QTimer::start(20)
const int kHeadlineTicks = 0x14;    // 0x000649d4 UpdateHeadline(0x14)
const int kTouchMoveIntervalMs = 0x14;  // 0x00065238 SetMoveInterval(0x14)

VDisplayTask* g_displayTask = 0;    // 0x00973fcc

} // namespace

// 0x00063344 VDisplayTask::VDisplayTask
VDisplayTask::VDisplayTask() : m_multitouch(0), m_display_timer(0) {
    g_displayTask = this;
}

VDisplayTask::~VDisplayTask() {
    if (g_displayTask == this) g_displayTask = 0;
}

// 0x000633a4 VDisplayTask::Instance
VDisplayTask* VDisplayTask::Instance() {
    return g_displayTask;
}

// Must run after VControlTask::Init (the proxies exist, and the igdtTask
// render thread it starts reads VDisplayProxy on its first frame).
// 0x00065238 VDisplayTask::Init
void VDisplayTask::Init() {
    VPageManager::Instance();
    igdtTask::Instance();
    // TODO(phase3): 0x001b0320 instance (camera display).
    m_multitouch = new VMultiTouch();
    m_multitouch->Init();
    m_multitouch->SetMoveInterval(kTouchMoveIntervalMs);
    qRegisterMetaType<GFX_MGR02_TOUCH_QDATA>("GFX_MGR02_TOUCH_QDATA");
    // TODO(phase3): DISPLAY_MSG / VMULTITOUCH_RAW_DATA / HMI_MEDIA_DEVICE and
    // their subsystem, SWDL, MirrorLink, camera and remote-key connections.
    connect(this, SIGNAL(SigInitialize()), this, SLOT(InitializeSlot()), Qt::QueuedConnection);
    connect(this, SIGNAL(SigShowPage(unsigned char)), this, SLOT(OnShowPage(unsigned char)), Qt::QueuedConnection);
    connect(this, SIGNAL(SigForceShowPage(unsigned char)), this, SLOT(OnForceShowPage(unsigned char)),
            Qt::QueuedConnection);
    connect(this, SIGNAL(SigClosePage(unsigned char)), this, SLOT(OnClosePage(unsigned char)), Qt::QueuedConnection);
    connect(this, SIGNAL(SigRemoveAllPage()), this, SLOT(OnRemoveAllPage()), Qt::QueuedConnection);
    connect(m_multitouch, SIGNAL(SigTouchEvent(VMULTITOUCH_DATA)), this, SLOT(OnTouchEvent(VMULTITOUCH_DATA)),
            Qt::QueuedConnection);
    connect(this, SIGNAL(SigIgdtTouchEvent(uint, GFX_MGR02_TOUCH_QDATA)), this,
            SLOT(OnIgdtTouchEvent(uint, GFX_MGR02_TOUCH_QDATA)));
    connect(VControlTask::Instance(), SIGNAL(SigKeyEventNotify(unsigned char, unsigned char)), this,
            SLOT(SlotKeyEventNotify(unsigned char, unsigned char)));
    // DEVIATION: stock's callback (0x00063298) is a fixed IGDT hook and
    // VDisplayProxy emits this task's signals directly; ours registers both.
    hmi_wrapper_igdt_set_touch_callback(&VDisplayTask::IgdtTouchCallback, this);
    VDisplayProxy::Instance()->SetDisplaySink(this);
    VControlTask::Instance()->SetPageQuery(VPageManager::Instance());
}

// 0x00063584 VDisplayTask::Run
void VDisplayTask::Run() {
    if (m_multitouch) m_multitouch->Run();
    Start();
    LogThread();
    emit SigInitialize();
}

// 0x000633b4 VDisplayTask::InitializeSlot
void VDisplayTask::InitializeSlot() {
    m_display_timer = new QTimer(0);
    connect(m_display_timer, SIGNAL(timeout()), this, SLOT(OnDisplayTimeOut()), Qt::QueuedConnection);
    // TODO(phase3): the touch-scan, page-status and unsupported-popup timers
    // and the media / MFL / App-Connect / climate / phone-stack connections.
    m_display_timer->start(kDisplayTickMs);
}

// Runs on the igdtTask render thread under the IGDT lock: only queue.
// 0x00063298 VDisplayTask::IgdtTouchCallback
void VDisplayTask::IgdtTouchCallback(uint32_t tag, const IgdtTouchEvent& ev, void* ctx) {
    VDisplayTask* task = static_cast<VDisplayTask*>(ctx);
    if (!task) return;
    GFX_MGR02_TOUCH_QDATA data = {ev.x, ev.y, ev.type, {0, 0, 0, 0}};
    emit task->SigIgdtTouchEvent(tag, data);
}

// 0x0006497c VDisplayTask::OnShowPage
void VDisplayTask::OnShowPage(unsigned char scr_id) {
    VPageManager::Instance()->ShowPage(scr_id);
}

// 0x00064994 VDisplayTask::OnForceShowPage
void VDisplayTask::OnForceShowPage(unsigned char scr_id) {
    VPageManager::Instance()->ForceShowPage(scr_id);
}

// 0x000649ac VDisplayTask::OnClosePage
void VDisplayTask::OnClosePage(unsigned char scr_id) {
    VPageManager::Instance()->ClosePage(scr_id);
}

// 0x000649c4 VDisplayTask::OnRemoveAllPage
void VDisplayTask::OnRemoveAllPage() {
    VPageManager::Instance()->RemoveAllPage();
}

// 0x000649d4 VDisplayTask::OnDisplayTimeOut
void VDisplayTask::OnDisplayTimeOut() {
    VPageManager::Instance()->UpdateHeadline(kHeadlineTicks);
    // TODO(phase3): the countdowns at +0x28 (VPowerProxy 0x000567a0(1)
    // unless power state 2) and +0x2c (VDisplayProxy::UpdateBacklight).
}

// Single-finger touch (gesture 0) to the IGDT touch queue: state 1 press,
// 2 release, 3 move become raw types 3, 5 and 4.
// 0x00065308 VDisplayTask::OnTouchEvent
void VDisplayTask::OnTouchEvent(VMULTITOUCH_DATA data) {
    if (data.gesture != 0) return;
    if (data.state[0] == 1) {
        hmi_wrapper_igdt_touch_event(data.x[0], data.y[0], 3);
    } else if (data.state[0] == 2) {
        hmi_wrapper_igdt_touch_event(data.x[0], data.y[0], 5);
    } else if (data.state[0] == 3) {
        hmi_wrapper_igdt_touch_event(data.x[0], data.y[0], 4);
    }
    // TODO(phase3): the two / three finger gesture flag (+0x34, 0x00064cf0).
}

// 0x00065524 VDisplayTask::OnIgdtTouchEvent
void VDisplayTask::OnIgdtTouchEvent(uint tag, GFX_MGR02_TOUCH_QDATA data) {
    const IgdtTouchEvent ev = {data.x, data.y, data.type};
    VPageManager::Instance()->OnIgdtTouchEvent(tag, ev);
}

// 0x000657dc VDisplayTask::SlotKeyEventNotify
void VDisplayTask::SlotKeyEventNotify(unsigned char key, unsigned char state) {
    hmi_wrapper_igdt_post_button_event(key, state);
}
