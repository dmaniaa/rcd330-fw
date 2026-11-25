// Stock source: control_logic/vcontroltask.cpp
//
// Phase 2 ports the power and hard-key paths. Every other stock slot
// (audio, AMFM, media, BT, camera, source change, radio preset / MirrorLink
// timers, navigation notifications) and its signal are TODO(phase3); their
// notifications never reach this task yet.
#include "control_logic/vcontroltask.h"

#include <QtCore/QMetaType>
#include <QtCore/QTimer>

#include "control_logic/input/vkeydriver.h"
#include "control_logic/mode/audio/vaudionotifyfilter.h"
#include "control_logic/mode/camera/vcameranotifyfilter.h"
#include "control_logic/mode/display/vdisplaycmdfilter.h"
#include "control_logic/mode/ipcl/vipclnotifyfilter.h"
#include "control_logic/mode/media/vmedianotifyfilter.h"
#include "control_logic/mode/power/vpowernotifyfilter.h"
#include "control_logic/mode/vprehandlefilter.h"
#include "control_logic/subsystem/amfm/vamfmproxy.h"
#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/audio/vaudioproxy.h"
#include "control_logic/subsystem/bap/vbapproxy.h"
#include "control_logic/subsystem/diag/vdiagproxy.h"
#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "control_logic/subsystem/media/vmediaproxy.h"
#include "control_logic/subsystem/phase3_stubs.h"
#include "control_logic/subsystem/pss/vpssproxy.h"
#include "control_logic/subsystem/system_time_calendar/vsystemtimecalendarproxy.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "display/igdt/igdt_gl.h"
#include "service/osal/osal_api.h"

#define CONTROL_LOG(func, line, ...)                                              \
    do {                                                                          \
        log_simple("INFO", __VA_ARGS__);                                          \
        printlog(3, "INFO", "control_logic/vcontroltask.cpp", func, line, __VA_ARGS__); \
    } while (0)

namespace {

// 0x009704e4 g_controlTask (set by the constructor, read by Instance).
VControlTask* g_controlTask = 0;

// Stock key code -> IPCL key table at 0x003ecb38 (identity for 0..13).
const unsigned kKeyMapSize = 14;

const uint8_t kKeyTuneUp = 7;
const uint8_t kKeyTuneDown = 8;
const uint8_t kKeyTune = 6;
const uint8_t kKeyVoice = 3;

// Key timer re-arm steps (0x00031ed8).
const int kKeyTick1 = 1000;
const int kKeyTick2 = 2000;
const int kKeyTick3 = 3000;

const uint8_t kPopKeyBlock = 0x2a;  // closed by a release while the HMI runs off
const uint8_t kAudioModePhone = 9;
const uint8_t kPlatformLowline = 2;
const int kRegionRow = 2;

}  // namespace

// 0x0002fd18 VControlTask::VControlTask
// Stock builds fifteen filters (+0x20..+0x58); the phase-2 chain is
// {power, IPCL, audio, media, camera} notify -> prehandle -> display cmd.
// TODO(phase3): VAudioCmdFilter, VDisplayNotifyFilter, VBTNotify / VBTCmd,
// VMediaCmd, VAMFMNotify / VAMFMCmd and VSubsystemNotify, and their links.
VControlTask::VControlTask()
    : m_key_timer(0),
      m_key_tick_ms(0),
      m_audio_notify(0),
      m_ipcl_notify(0),
      m_prehandle(0),
      m_power_notify(0),
      m_media_notify(0),
      m_camera_notify(0),
      m_display_cmd(0),
      m_dev_sts_b45(0),
      m_dev_sts_b01(0),
      m_dev_sts_b23(0),
      m_dev_sts_b67(0),
      m_page_query(0),
      m_logged_bad_key(false),
      m_logged_bap_key(false) {
    m_audio_notify = new VAudioNotifyFilter(this);
    m_display_cmd = new VDisplayCmdFilter(this);
    m_ipcl_notify = new VIPCLNotifyFilter(this);
    m_prehandle = new VPrehandleFilter(this);
    m_power_notify = new VPowerNotifyFilter(this);
    m_media_notify = new VMediaNotifyFilter(this);
    m_camera_notify = new VCameraNotifyFilter(this);

    m_prehandle->AddNextFilter(m_display_cmd);
    m_audio_notify->AddNextFilter(m_prehandle);
    m_ipcl_notify->AddNextFilter(m_prehandle);
    m_power_notify->AddNextFilter(m_prehandle);
    m_media_notify->AddNextFilter(m_prehandle);
    m_camera_notify->AddNextFilter(m_prehandle);
    g_controlTask = this;
}

// DEVIATION: stock never destroys VControlTask; ours frees the filters.
VControlTask::~VControlTask() {
    if (g_controlTask == this) {
        g_controlTask = 0;
    }
    delete m_camera_notify;
    delete m_media_notify;
    delete m_power_notify;
    delete m_prehandle;
    delete m_ipcl_notify;
    delete m_display_cmd;
    delete m_audio_notify;
    delete m_key_timer;
}

// 0x00030054 VControlTask::Instance
VControlTask* VControlTask::Instance() {
    return g_controlTask;
}

// 0x00033c68 VControlTask::Init
void VControlTask::Init() {
    CONTROL_LOG("Init", 0x109, " ==== HMI ==== INFO: VControlTask::Init start");
    VPowerProxy::Instance()->Init();
    VDisplayProxy::Instance()->Init();
    VAudioProxy::Instance()->Init();
    VAMFMProxy::Instance()->Init();
    VMediaProxy::Instance()->Init();
    VMirrorLinkProxy::Instance()->Init();
    VCarPlayProxy::Instance()->Init();
    VBTProxy::Instance()->Init();
    VSubsystemProxy::Instance()->Init();
    VBAPProxy::Instance()->Init();
    VDiagProxy::Instance()->Init();
    VServiceProxy::Instance()->Init();
    VServiceAuxProxy::Instance()->Init();
    VSystemTimeCalendarProxy::Instance()->Init();
    VPSSProxy::Instance()->Init();
    VAppConnectProxy::Instance()->Init();
    VCarLifeProxy::Instance()->Init();
    VAppConnectPopupProxy::Instance()->Init();
    VAndroidAutoProxy::Instance()->Init();
    // DEVIATION: stock VPowerProxy calls VControlTask::Instance() directly;
    // ours hands it the sink. Done before VmfThread starts dispatching.
    VPowerProxy::Instance()->SetPowerNotifySink(this);
    // DEVIATION (ours): the PSS setup-read retry runs on this task's thread.
    VPSSProxy::Instance()->SetSetupReadRetryThread(Thread());
    RegisterSignals();
    emit SigInitialize();
    CONTROL_LOG("Init", 0x121, " ==== HMI ==== INFO: VControlTask::Init end");
}

// 0x0003399c VControlTask::RegisterSignals
// Name is ours. TODO(phase3): the other fourteen stock connections
// (subsystem, audio, AMFM, media, display, camera, BT, source change,
// backlight delay, BAP language, radio preset and MirrorLink timers, navi).
void VControlTask::RegisterSignals() {
    qRegisterMetaType<FILTER_MSG>("FILTER_MSG");
    connect(this, SIGNAL(SigInitialize()), this, SLOT(InitializeSlot()));
    connect(this, SIGNAL(SigPowerSubsystemNotifyMsg(FILTER_MSG)), this, SLOT(PowersubsystemNotifySlot(FILTER_MSG)));
    connect(this, SIGNAL(SigKeySubsystemNotifyMsg(FILTER_MSG)), this, SLOT(KeysubsystemNotifySlot(FILTER_MSG)));
    // DEVIATION: stock key drivers call KeyDriverNotifySlot directly on their
    // own thread; ours queue it to the task thread (PostKeyEvent).
    connect(this, SIGNAL(SigKeyDriverNotify(unsigned short, int)), this, SLOT(KeyDriverNotifySlot(unsigned short, int)),
            Qt::QueuedConnection);
    // Stock connects SigIgdtRefreshNotify in VDisplayTask (Task 9).
}

// 0x00030064 VControlTask::Run
void VControlTask::Run() {
    Start();
    CONTROL_LOG("Run", 0x9b, " ==== HMI ==== INFO: VControlTask::Run");
    LogThread();
}

// 0x00035624 VControlTask::InitializeSlot
// TODO(phase3): the other fifteen stock timers (source, popup, scan, play
// command, BT connect, unmounting, audio request, BAP ack, ...).
void VControlTask::InitializeSlot() {
    m_key_timer = new QTimer(0);
    connect(m_key_timer, SIGNAL(timeout()), this, SLOT(HMIKeyTimeOutSlot()));
}

void VControlTask::SetPageQuery(IPageQuery* query) {
    m_page_query = query;
}

void VControlTask::PostKeyEvent(uint8_t code, uint8_t state) {
    emit SigKeyDriverNotify(code, state);
}

// 0x00033244 VControlTask::KeySubsystemNotifyMsg
void VControlTask::KeySubsystemNotifyMsg(const FILTER_MSG& msg) {
    emit SigKeySubsystemNotifyMsg(msg);
}

// 0x000306e0 VControlTask::StartKeyTimer
void VControlTask::StartKeyTimer(int ms) {
    if (m_key_timer == 0) {
        return;
    }
    if (m_key_timer->isActive()) {
        m_key_timer->stop();
    }
    m_key_timer->start(ms);
    m_key_tick_ms = ms;
}

// 0x00030768 VControlTask::StopKeyTimer
void VControlTask::StopKeyTimer() {
    if (m_key_timer != 0 && m_key_timer->isActive()) {
        m_key_timer->stop();
    }
}

// 0x00031ed8 VControlTask::HMIKeyTimeOutSlot
void VControlTask::HMIKeyTimeOutSlot() {
    if (m_ipcl_notify == 0) {
        return;
    }
    const int tick = m_key_tick_ms;
    m_ipcl_notify->KeyTimeout(static_cast<uint32_t>(tick));
    if (tick == kKeyTick1) {
        StartKeyTimer(kKeyTick1);
    } else if (tick == kKeyTick2) {
        StartKeyTimer(kKeyTick3);
    } else if (tick == kKeyTick3) {
        StartKeyTimer(5000);
    }
}

// 0x000322bc VControlTask::KeyDriverNotifySlot
// Turns a driver key into {notify, 3, {4, 1, key, 1 press | 2 release}} for
// the IPCL filter, after the HMI-off, diagnostics and knob gates.
// TODO(phase3): display mode 1 with region 2 (MirrorLink, 0x00051b74)
// routes only the tune key to 0x00051c40; CarPlay (screen 0x1d), Android
// Auto (screen 1) and CarLife (screen 0x1c) take keys 6..8 first; phone
// audio handles the keys outside {1..6, 10}.
void VControlTask::KeyDriverNotifySlot(unsigned short code, int value) {
    CONTROL_LOG("KeyDrvierSubsystemNotifyMsg", 0xcf2,
                " ==== HMI ==== INFO: VControlTask::KeySubsystemNotifyMsg===============key_code=%d,key_value=%d", code,
                value);
    if (code >= kKeyMapSize) {
        // DEVIATION: stock indexes its key table without a bound.
        if (!m_logged_bad_key) {
            m_logged_bad_key = true;
            CONTROL_LOG("KeyDrvierSubsystemNotifyMsg", 0xcf2, " ==== HMI ==== WARN: key code %d dropped (logged once)", code);
        }
        return;
    }
    VPowerProxy* power = VPowerProxy::Instance();
    VDisplayProxy* display = VDisplayProxy::Instance();
    FILTER_MSG msg;
    msg.Reset();
    msg.valid = kFilterNotify;
    msg.type = 3;
    msg.para[0] = kFilterSubIpcl;
    msg.para[1] = 1;
    const uint8_t key = static_cast<uint8_t>(code);  // identity key table
    msg.para[2] = key;
    if (value == 0) {
        msg.para[3] = 2;
        if (power->IsHMIOn() == 1) {
            display->UpdateBacklight();
        }
    } else if (value == 1) {
        msg.para[3] = 1;
        if (power->IsHMIOn() == 1) {
            // TODO(phase3): VDisplayProxy 0x00050c58 (clears the backlight
            // timer +0x50) is not ported.
            if (key == kKeyTuneUp || key == kKeyTuneDown) {
                display->UpdateBacklight();
            }
        }
    }

    if (power->IsHMIOn() == 0 && power->GetTempProtect() == 0) {
        if ((msg.para[3] == 2 && (key == kKeyVoice || key == kKeyTune)) || key == kKeyTuneUp || key == kKeyTuneDown) {
            // TODO(phase3): 0x00051710(300) sets the VDisplayTask (0x000633a4)
            // +0x2c tick count to 300 / 20 to wake the display; not ported.
            CONTROL_LOG("KeyDrvierSubsystemNotifyMsg", 0xdb6,
                        " ==== HMI ==== INFO: VControlTask::KeySubsystemNotifyMsg=======3333=====");
        }
        if (key >= kKeyTune && key <= kKeyTuneDown) {
            return;
        }
    }
    // TODO(phase3): VDiagProxy 0x001fce34 / 0x001fce44 (diagnostic sessions)
    // swallow every key.

    if (key == kKeyTuneUp || key == kKeyTuneDown) {
        // The knob never enters the filter chain: pages get (key, 3).
        CONTROL_LOG("KeyDrvierSubsystemNotifyMsg", 0xdc8,
                    " ==== HMI ==== FOR_FREEZE: 533269 KeyDrvierSubsystemNotifyMsg IPCL_HARDKEY_TUNE_UP/IPCL_HARDKEY_TUNE_DOWN");
        if (hmi_wrapper_igdt_get_mutihardkey_permit()) {
            emit SigKeyEventNotify(key, 3);
        }
        NotifyBapKey(key, 4);
        return;
    }

    if (power->GetHmiRunFlag() == 0 && power->GetRemoteWakeFlag() == 0) {
        // Before the HMI runs only the tune key (and keys in phone audio)
        // reach the filters; a release of a page key closes popup 0x2a.
        const bool page_key = key == 1 || key == 2 || key == 4 || key == 5 || key == 10;
        const bool voice_counts = key == kKeyVoice && power->GetPlatformType() == kPlatformLowline;
        if ((page_key || voice_counts) && msg.para[3] == 2) {
            display->ClosePage(kPopKeyBlock);
        }
        if (VAudioProxy::Instance()->GetAudioMode() != kAudioModePhone && key != kKeyTune) {
            return;
        }
    }
    emit SigKeySubsystemNotifyMsg(msg);
}

// 0x0003356c VControlTask::KeysubsystemNotifySlot
// TODO(phase3): display mode 1 with region 2 (0x00051b74) drops the key.
void VControlTask::KeysubsystemNotifySlot(FILTER_MSG msg) {
    CONTROL_LOG("KeysubsystemNotifySlot", 0xe83, " ==== HMI ==== INFO: VControlTask::KeysubsystemNotifySlot msg.para[2]=%d",
                msg.para[2]);
    m_ipcl_notify->DispatchMsg(msg);
}

// 0x00033298 VControlTask::PowersubsystemNotifySlot
void VControlTask::PowersubsystemNotifySlot(FILTER_MSG msg) {
    m_power_notify->DispatchMsg(msg);
}

void VControlTask::EmitPowerMsg(const FILTER_MSG& msg) {
    emit SigPowerSubsystemNotifyMsg(msg);
}

// 0x00034630 VControlTask::PowersubsystemNotifyMsg
// VMF thread, VmfThread listener mutex held: only proxy state is touched and
// the message is queued to the task thread.
// DEVIATION: data bytes are read only when len covers them (stock does not
// check).
void VControlTask::PowersubsystemNotifyMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) {
    if (data == 0) {
        return;
    }
    FILTER_MSG msg;
    msg.Reset();
    msg.valid = kFilterNotify;
    msg.type = 3;
    msg.para[0] = kFilterSubPower;
    const bool row_lowline = VAppConnectProxy::Instance()->GetRegionMode() == kRegionRow &&
                             VPowerProxy::Instance()->GetPlatformType() == kPlatformLowline;

    if (grp == 0x0d) {
        // VIP aux-in detect: para {2, 3, state} (aux status).
        if (sub == 8 && !row_lowline && len >= 1) {
            msg.para[1] = 3;
            msg.para[2] = data[0];
        }
        EmitPowerMsg(msg);
        return;
    }
    if (grp == 0x0c) {
        if (sub == 4 && len >= 2) {
            // TODO(phase3): device plug / unplug turns into source commands
            // ({1, 2, {0x0c|0x04|0x05|0x03, 1|4}}) from the +0xd1..+0xd4 edges.
            const uint8_t b = data[1];
            m_dev_sts_b01 = b & 3;
            m_dev_sts_b23 = (b & 0x0c) >> 2;
            m_dev_sts_b45 = (b & 0x30) >> 4;
            m_dev_sts_b67 = b >> 6;
        }
        EmitPowerMsg(msg);
        return;
    }
    if (grp == 0x6e) {
        EmitPowerMsg(msg);
        return;
    }
    if (grp != 0x74) {
        return;
    }
    if (sub != 1 || len < 1) {
        EmitPowerMsg(msg);
        return;
    }
    const uint8_t id = data[0];
    if (id == 0x82) {
        // MSG_VIPHMI_APHMI_AUX_STATE: para {2, 3, state}.
        if (!row_lowline && len >= 2) {
            msg.para[1] = 3;
            msg.para[2] = data[1];
        }
        EmitPowerMsg(msg);
        return;
    }
    if (id == 0x81 && len >= 5) {
        // MSG_VIPHMI_APHMI_AUDIO_MODE {0x81, cur, target, main page, prev page}.
        // TODO(phase3): stock stops the audio request timer (+0x78) first.
        VPowerProxy* power = VPowerProxy::Instance();
        VAudioProxy* audio = VAudioProxy::Instance();
        const int audio_mode = audio->GetAudioMode();
        if (audio_mode == 0x0e || audio_mode == 0x11 || audio_mode == 9 || audio_mode == 0x0d) {
            // Stock 0x00034818: these audio modes keep the current page; only
            // the start-up handshake runs, and nothing reaches the filters.
            if (IgdtFirstFrameSwapped()) {
                power->SendWakeStateToPowerAgent(2);
            }
            power->SetStartupDone(2);
            power->GetPwrCurStateToPowerAgent();
            // TODO(phase3): 0x000345dc -> 0x000540b0.
            return;
        }
        // The current primary audio channel is the VIP's current source
        // (stock 0x00034d04); the RUN power mode later builds its connection
        // and shows its main page (VPowerProxy::HandleRunMode).
        uint8_t source = data[1];
        if (source == 8 || source == 0x0d) {
            // TODO(phase3): kept while BT 0x000c89a8 reports a phone; phase 2
            // has no BT proxy, so stock's no-phone fallback (tuner) applies.
            source = 1;
        } else if (source < 1 || source > 0x10) {
            source = 1;
        }
        // TODO(phase3): sources 4..7 consult the App-Connect region, the
        // power state at 0x0005a560 and +0xd1 / +0xd3 and may take data[2] or 0x10; the PSS
        // sync (SyncAudioModeFromPSS 0x00046e0c) of cur / target and the media
        // source (0x00046fc8) follow.
        audio->SetCurPrimaryAudioChannel(source);
        VDisplayProxy* display = VDisplayProxy::Instance();
        int main_page = data[3];
        if (main_page == 8) {
            // The App-Connect menu needs a connected phone; else the
            // no-connection page.
            if (!VAppConnectProxy::Instance()->IsAppDeviceConnected()) {
                main_page = 9;
            }
        } else if (main_page == 0) {
            main_page = 1;
        }
        // TODO(phase3): BT (0x000c89a8 / 0x000c89f8) overrides the page for
        // sources 8 / 0x0d and pages 4 / 10.
        display->SetMainPageValue(main_page);
        display->SetPrevMainPageValue(data[4]);
        CONTROL_LOG("PowersubsystemNotifyMsg", 0x5fc,
                    " ==== HMI ==== INFO: VControlTask::PowersubsystemNotifyMsg::MSG_VIPHMI_APHMI_AUDIO_MODE %d %d %d %d",
                    data[1], data[2], data[3], data[4]);
        // Wake state 2 only once the first frame was swapped; before that the
        // swap itself reports it (IgdtSwap, 0x00326b3c -> 0x000560e4).
        if (IgdtFirstFrameSwapped()) {
            power->SendWakeStateToPowerAgent(2);
        }
        power->SetStartupDone(2);
        power->GetPwrCurStateToPowerAgent();
        // TODO(phase3): 0x000345dc -> 0x000540b0.
        EmitPowerMsg(msg);
        return;
    }
    if (id == 0x83 && len >= 2) {
        // Steering-wheel / VIP key events.
        // TODO(phase3): 'U' / 'V' (BAP 0x0b / 0x0c popup 0x88) while the HMI
        // runs or after a remote wake.
        if (data[1] == 'S') {
            msg.para[1] = 4;
            msg.para[2] = 0;
            NotifyBapKey(9, 4);
        } else if (data[1] == 0x93) {
            msg.para[1] = 4;
            msg.para[2] = 1;
            NotifyBapKey(9, 5);
        }
    }
    EmitPowerMsg(msg);
}

// 0x00036270 VControlTask::OnIgdtRefreshNotify
// TODO(phase3): for screen 0xdf, closes the camera (0x0004bdb4, 0x0004d20c)
// unless the BAP RVC mode (0x000b6ca8) is 3..5.
void VControlTask::OnIgdtRefreshNotify(unsigned char scr_id) {
    (void)scr_id;
}

// 0x00032a60 VControlTask::NotifyDisplayKey
// Name is ours. TODO(phase3): dropped in display mode 1 with region 2
// (0x00051b74).
void VControlTask::NotifyDisplayKey(uint8_t key, uint8_t state) {
    emit SigKeyEventNotify(key, state);
}

// TODO(phase3): VBAPProxy 0x000b014c reports hard keys to the cluster.
void VControlTask::NotifyBapKey(uint8_t key, uint8_t state) {
    if (!m_logged_bap_key) {
        m_logged_bap_key = true;
        CONTROL_LOG("NotifyBapKey", 0, " ==== HMI ==== INFO: BAP key report (%d,%d) is phase 3 (logged once)", key, state);
    }
}

void VControlTask::SetMainPage(int audio_src, int force) {
    VDisplayProxy::Instance()->SetMainPage(audio_src, force);
}

void VControlTask::InsertPage(int page_id) {
    VDisplayProxy::Instance()->InsertPage(page_id);
}

void VControlTask::ClosePopPage(uint8_t pop_id) {
    VDisplayProxy::Instance()->ClosePage(pop_id);
}

void VControlTask::NotifyHmiOn() {
    VDisplayProxy::Instance()->NotifyHmiOn();
}

int VControlTask::GetCurDisplayPage() const {
    return VDisplayProxy::Instance()->GetCurDisplayPage();
}

bool VControlTask::IsIgdtShown() const {
    return VDisplayProxy::Instance()->IsIgdtShown() != 0;
}

void VControlTask::UpdateBacklight() {
    VDisplayProxy::Instance()->UpdateBacklight();
}

int VControlTask::GetCurrentScreen() const {
    return m_page_query != 0 ? m_page_query->GetCurrentScreen() : 0;
}

int VControlTask::GetBaseScreen() const {
    return m_page_query != 0 ? m_page_query->GetBaseScreen() : 0;
}

bool VControlTask::IsScreenShown(uint8_t scr_id) const {
    return m_page_query != 0 && m_page_query->IsScreenShown(scr_id);
}

bool VControlTask::IsScreenInStack(uint8_t scr_id) const {
    return m_page_query != 0 && m_page_query->IsScreenInStack(scr_id);
}

int VControlTask::GetAudioMode() const {
    return VAudioProxy::Instance()->GetAudioMode();
}

void VControlTask::SetAudioMode(int mode) {
    VAudioProxy::Instance()->InsertAudioMode(static_cast<uint8_t>(mode));
}

void VControlTask::QueryTunerInfo() {
    VAMFMProxy* amfm = VAMFMProxy::Instance();
    amfm->OnGetCurrentInfoOpera();
    amfm->OnGetTunerListOpera();
    amfm->OnGetSeekmodeOpera();
}

// TODO(phase3): VAMFMProxy 0x000af314 (band, +0x12); FM until then.
int VControlTask::GetTunerBand() const {
    return 1;
}

// TODO(phase3): VAMFMProxy 0x000af304 (tuner state, +0x14); idle until then.
int VControlTask::GetTunerState() const {
    return 0;
}

bool VControlTask::IsHMIOn() const {
    return VPowerProxy::Instance()->IsHMIOn() == 1;
}

bool VControlTask::IsTempProtect() const {
    return VPowerProxy::Instance()->GetTempProtect() != 0;
}

int VControlTask::GetAuxMode() const {
    return VPowerProxy::Instance()->GetAuxMode();
}

int VControlTask::GetRegionMode() const {
    return VAppConnectProxy::Instance()->GetRegionMode();
}

// TODO(phase3): VAppConnectProxy 0x00347580 (connected device type).
int VControlTask::GetAppDeviceType() const {
    return 0;
}

// TODO(phase3): VMediaProxy 0x000d7088.
int VControlTask::GetMediaDeviceFlags() const {
    return 0;
}

// TODO(phase3): VMirrorLinkProxy 0x000dca70.
int VControlTask::GetMirrorLinkState() const {
    return 0;
}

// Stock reads VKeyDriver (0x000aa64c) +0x10.
int VControlTask::GetActiveKeyCode() const {
    const VKeyDriver* keys = VKeyDriver::Instance();
    // DEVIATION: stock dereferences the driver unchecked; main creates it
    // before any key can arrive, so -1 (no key) only covers start-up.
    return keys != 0 ? keys->GetKeyCode() : -1;
}

bool VControlTask::IsMultiHardkeyPermit() const {
    return hmi_wrapper_igdt_get_mutihardkey_permit();
}
