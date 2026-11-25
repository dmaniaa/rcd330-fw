#include "display/igdt/extend/igdt_wrapper.h"

#include <QtCore/QTimer>

#include <pthread.h>
#include <string.h>

#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "control_logic/vcontroltask.h"
#include "display/igdt/igdt_gl.h"
#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_scene.h"
#include "display/igdt/igdt_strings.h"
#include "display/igdt/igdt_text.h"
#include "display/page/base/basepage.h"
#include "display/page/base/pagemanager.h"
#include "service/osal/osal_api.h"

namespace {

const char kFile[] = "display/igdt/extend/igdt_wrapper.cpp";
const unsigned kTextIdCount = 0xe3f;        // text ids the runtime text store accepts
const uint32_t kNoStrId = 0xffffffffu;
const int kLangCount = 5;                   // 0x0031d210 accepts 0..4
const int kReflashMs = 32;                  // 0x00062fdc QTimer::start(32)
const unsigned kRenderStackSize = 0x20000;  // 0x0096996c

bool g_igdtShown = false;   // 0x00973fc1
bool g_igdtGlReady = false;

pthread_mutex_t g_igdtMutex;    // 0x00a0fa40
bool g_igdtMutexReady = false;

IgdtTouchState g_touchState;    // 0x00df28b8..
IgdtTouchCallback g_touchCb = 0;
void* g_touchCtx = 0;

int g_igdtLang = 0;                     // 0x00a0f774
uint32_t g_textStrIds[kTextIdCount];    // 0x009705a8, kNoStrId = not from a string id

bool g_hardkeyPermit = true;        // 0x00969968 b_hardkey_permit
bool g_touchPermit = true;          // 0x00969969 b_touch_permit
bool g_mutiHardkeyPermit = true;    // 0x0096996a

bool g_pageShown[256];          // 0x00973ec0: screen shown by insert, cleared by remove
uint16_t g_heldKey = 0xffff;    // 0x00a0f76c: key whose press is being tracked

igdtTask* g_igdtTask = 0;   // 0x00973fc4

// Stock lock / unlock (0x003212a0 / 0x003212ac) around one scope. Name is ours.
class IgdtLock {
public:
    IgdtLock() { pthread_mutex_lock(&g_igdtMutex); }
    ~IgdtLock() { pthread_mutex_unlock(&g_igdtMutex); }
};

void LogInfo(const char* func, int line, const char* tag, const char* msg) {
    // The QEMU log_simple hook (tools/arm_hal_shim) only echoes formats that
    // contain the " ==== HMI ==== " marker, so keep it in the format.
    static const char kMarker[] = " ==== HMI ==== ";
    if (strncmp(msg, kMarker, sizeof(kMarker) - 1) == 0) {
        log_simple(tag, " ==== HMI ==== %s", msg + sizeof(kMarker) - 1);
    } else {
        log_simple(tag, "%s", msg);
    }
    printlog(3, tag, kFile, func, line, "%s", msg);
}

// A runtime text change makes the next show redraw (stock: DAT_00a0fa58 = 1).
int MarkTextDirty(int ret) {
    if (ret == IGDT_OK) IgdtGetRuntime().dirty = true;
    return ret;
}

// 0x00320ec8 SetTextFromId (under the lock)
int SetTextFromId(unsigned id, uint32_t str_id) {
    return MarkTextDirty(IgdtSetRuntimeTextId(g_igdtTablesVw, g_igdtStringsVw, id, str_id, g_igdtLang));
}

} // namespace

// DEVIATION: stock's cold init also printf()s the glibc mutexattr bytes.
// 0x00320c7c IgdtColdInit
static void IgdtColdInit() {
    if (!g_igdtMutexReady) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0) {
            pthread_mutex_init(&g_igdtMutex, &attr);
            g_igdtMutexReady = true;
        }
    }
    IgdtLock lock;
    IgdtRuntimeInit(IgdtGetRuntime(), g_igdtTablesVw);
    IgdtTouchInit(g_touchState);
}

// 0x00060e3c hmi_wrapper_igdt_init
void hmi_wrapper_igdt_init() {
    LogInfo("hmi_wrapper_igdt_init", 0x8b, "INFO", " ==== HMI ==== INFO: Started..\n");
    IgdtColdInit();
    LogInfo("hmi_wrapper_igdt_init", 0x8e, "INFO", " ==== HMI ==== INFO: coldinit done..\n");
    // TODO(phase3): 0x003216fc IGDT interface start (update state machine).
    LogInfo("hmi_wrapper_igdt_init", 0x91, "INFO", " ==== HMI ==== INFO: if_start done..\n");
    // TODO(phase3): 0x0031bc38(1).
    g_igdtLang = 1;     // 0x0031d210(1)
    for (unsigned i = 0; i < kTextIdCount; ++i) g_textStrIds[i] = kNoStrId;
    // TODO(phase3): memset(0x00973ec0, 0, 0xff) page flags, DAT_00973fbf = 0xff.
}

// DEVIATION: stock's update (0x003216c4 -> 0x00321328) runs IGDT's frame
// state machine and redraws dirty rects; ours drains the touch queue and
// redraws everything when the runtime changed.
// 0x00060f54 hmi_wrapper_igdt_show
void hmi_wrapper_igdt_show() {
    // TODO(phase3): DAT_00973fc0 = 1 (show in progress).
    if (!g_igdtShown) {
        g_igdtShown = true;
        // 0x0031d60c: draw-state reset, then 0x00326084.
        // TODO(phase3): the rest of 0x0031d60c (DAT_00a0fa38 = 0, DAT_00a0fa34 = 0x00321f5c()).
        g_igdtGlReady = IgdtGlInit(800, 480);
        if (!g_igdtGlReady) {
            // DEVIATION: stock ignores 0x00326084's result.
            log_simple("INFO", " ==== HMI ==== INFO: IGDT GL init failed\n");
        }
        // DEVIATION: stock starts FreeType lazily on the first text draw
        // (0x0031d2a4); font paths are relative to /opt like the textures.
        IgdtTextInit("/opt");
        LogInfo("hmi_wrapper_igdt_show", 0xdd, "INFO", " ==== HMI ==== INFO: igdt_show \n");
        VDisplayProxy::Instance()->SetIgdtShown(1);
    }
    {
        IgdtLock lock;
        IgdtRuntime& rt = IgdtGetRuntime();
        // The recursive mutex lets callbacks call the setters.
        IgdtTouchProcessQueue(g_touchState, g_igdtTablesVw, rt, g_touchCb, g_touchCtx);
        if (g_igdtGlReady && IgdtConsumeDirty(rt)) {
            IgdtSceneDrawLayers(g_igdtTablesVw, rt);
            IgdtSwap();
        }
    }
    // TODO(phase3): when DAT_00973ea4 is set: 0x00320d48, 0x0032177c and the
    // VPowerProxy (0x00055fd4) / VPageManager (0x00052174) follow-ups.
    // TODO(phase3): DAT_00973fc0 = 0
}

bool hmi_wrapper_igdt_gl_ready() {
    return g_igdtGlReady;
}

// 0x000610a8 hmi_wrapper_igdt_insert_page
void hmi_wrapper_igdt_insert_page(uint8_t screen_id, int mode, void* page) {
    const uint8_t layer = IgdtScreenLayer(g_igdtTablesVw, screen_id);
    VPageManager::Instance()->InsertPage(screen_id, mode, layer, static_cast<VBasePage*>(page));
    if (g_hardkeyPermit) return;
    LogInfo("hmi_wrapper_igdt_insert_page", 0x143, "FOR_FREEZE",
            " ==== HMI ==== FOR_FREEZE: 533269 hmi_wrapper_igdt_insert_page   b_hardkey_permit false");
    IgdtLock lock;
    IgdtTouchInvalidateForScreen(g_touchState, screen_id);
}

// DEVIATION: it also cancels a touch captured on that screen (stock: only
// insert does), so no click reaches a removed page.
// 0x000611bc hmi_wrapper_igdt_remove_page
void hmi_wrapper_igdt_remove_page(uint8_t screen_id, int mode) {
    (void)mode;
    VPageManager::Instance()->RemovePage(screen_id);
    IgdtLock lock;
    // TODO(phase3): screen 0xdf sets DAT_00973fbf = 0xdf and clears
    // uRam00973f9f instead of the page flag.
    IgdtLayerHideScreen(IgdtGetRuntime(), g_igdtTablesVw, screen_id);
    if (screen_id != IGDT_SCREEN_NONE) g_pageShown[screen_id] = false;
    IgdtTouchInvalidateForScreen(g_touchState, screen_id);
}

// Name is ours: the IGDT half of VPageManager::ShowIgdtScreen (0x000611e4).
void hmi_wrapper_igdt_show_screen(uint8_t screen_id) {
    IgdtLock lock;
    IgdtLayerShowScreen(IgdtGetRuntime(), g_igdtTablesVw, screen_id);
    if (screen_id != IGDT_SCREEN_NONE) g_pageShown[screen_id] = true;
}

// 0x00061220 hmi_wrapper_igdt_is_page_shown
bool hmi_wrapper_igdt_is_page_shown(uint8_t screen_id) {
    IgdtLock lock;
    return g_pageShown[screen_id];
}

// DEVIATION: stock queues raw types other than 3/4/5 with an uninitialized
// type; ours drops them.
// 0x000612d4 hmi_wrapper_igdt_touch_event
void hmi_wrapper_igdt_touch_event(int32_t x, int32_t y, int raw_type) {
    IgdtTouchEvent ev = {x, y, IGDT_TOUCH_MOVE};
    if (raw_type == 4) {
        ev.type = IGDT_TOUCH_MOVE;
    } else if (raw_type == 5) {
        g_hardkeyPermit = true;
        ev.type = IGDT_TOUCH_RELEASE;
        LogInfo("hmi_wrapper_igdt_touch_event", 0x275, "FOR_FREEZE",
                " ==== HMI ==== FOR_FREEZE: 533269 hmi_wrapper_igdt_touch_event b_hardkey_permit = true");
    } else if (raw_type == 3) {
        ev.type = IGDT_TOUCH_PRESS;
        if (g_touchPermit) {
            LogInfo("hmi_wrapper_igdt_touch_event", 0x280, "FOR_FREEZE",
                    " ==== HMI ==== FOR_FREEZE: 533269 hmi_wrapper_igdt_touch_event  b_touch_permit = "
                    "true;b_hardkey_permit = false");
            g_hardkeyPermit = false;
        }
    } else {
        return;
    }
    // TODO(phase3): the power / phone gates (0x00055fd4 + 0x0005a0b8,
    // 0x001ffc10 + 0x001fce34 / 0x001fce44) that drop the event with
    // VPageManager 0x00050bcc, and 0x00050c58 / 0x00050bcc before queueing.
    if (!g_touchPermit) return;
    LogInfo("hmi_wrapper_igdt_touch_event", 0x2a4, "FOR_FREEZE",
            " ==== HMI ==== FOR_FREEZE: 533269 hmi_wrapper_igdt_touch_event  b_touch_permit = true,lock brefore");
    {
        IgdtLock lock;
        IgdtTouchEnqueue(g_touchState, ev);
    }
    LogInfo("hmi_wrapper_igdt_touch_event", 0x2a8, "FOR_FREEZE",
            " ==== HMI ==== FOR_FREEZE: 533269 hmi_wrapper_igdt_touch_event  b_touch_permit = true,lock end");
    VPageManager::Instance()->OnTouchEvent(x, y, raw_type);
}

void hmi_wrapper_igdt_set_touch_callback(IgdtTouchCallback cb, void* ctx) {
    IgdtLock lock;
    g_touchCb = cb;
    g_touchCtx = ctx;
}

// Stock 0x00062b84 (merged into the 0x000657dc export, so it has no
// origin tag): a hard key VControlTask reported (SigKeyEventNotify). IGDT
// screens may take it through their key handlers; otherwise it goes back to
// VControlTask as a re-posted key (0x000632bc) for the filter chain.
void hmi_wrapper_igdt_post_button_event(uint8_t key, uint8_t state) {
    if (!hmi_wrapper_igdt_get_hardkey_permit()) return;
    // TODO(phase3): with a touch down (b_hardkey_permit false) stock first
    // releases it for the MirrorLink / media sources (0x000bf734, 0x000d82e0).
    LogInfo("hmi_wrapper_igdt_post_button_event", 0x204, "FOR_FREEZE",
            " ==== HMI ==== FOR_FREEZE: 533269 hmi_wrapper_igdt_post_button_event   b_hardkey_permit true");
    if (key > 10) return;
    // One key at a time: another key's press-hold drops this one.
    if (g_heldKey != 0xffff && g_heldKey != key) return;
    if (g_heldKey == 0xffff) g_heldKey = key;
    // TODO(phase3): the IGDT screen key handlers (0x0031c5e0 table
    // 0x00452a68); no phase-2 screen has one, so no key is consumed here.
    if (state == 0) g_heldKey = 0xffff;
    FILTER_MSG msg;
    msg.Reset();
    msg.valid = kFilterNotify;
    msg.type = 4;
    msg.para[0] = 0x04;
    msg.para[1] = 0x01;
    msg.para[2] = key;
    msg.para[3] = state & 0x0f;
    // Stock 0x000632bc: re-post to the filter chain, then release the key
    // (without it the held key stayed set after APP's short release and
    // every other hard key was dropped until a state-0 event).
    VControlTask::Instance()->KeySubsystemNotifyMsg(msg);
    hmi_wrapper_igdt_release_key(key);
}

// 0x00061244 hmi_wrapper_igdt_release_key
// Name is ours. Stock calls 0x0031c5e0(key, 0xff): with focus 0xff it only
// forgets the held key (0x00a0f76c) if it is this one.
// TODO(phase3): 0x0031c5e0 with a real focus index (screen key handlers).
bool hmi_wrapper_igdt_release_key(uint8_t key) {
    if (g_heldKey == key) g_heldKey = 0xffff;
    return true;
}

// 0x000615ec hmi_wrapper_igdt_set_text_id
int hmi_wrapper_igdt_set_text_id(unsigned id, uint32_t str_id) {
    if (id >= kTextIdCount) return IGDT_ERR_RANGE;
    IgdtLock lock;
    g_textStrIds[id] = str_id;
    return SetTextFromId(id, str_id);
}

// Copies at most the layout's max_chars wchars and replaces 1, 2 and
// anything above U+10000 with 3.
// DEVIATION: stock copies max_chars wchars even past the terminator of a
// shorter string; ours stops at it. NULL is IGDT_ERR_PARAM (stock crashes).
// 0x00061510 hmi_wrapper_igdt_set_text_wstr
int hmi_wrapper_igdt_set_text_wstr(unsigned id, const IgdtWchar* wstr) {
    if (!wstr) return IGDT_ERR_PARAM;
    IgdtLock lock;
    // 0x00322488: max_chars of the layout, 0 for an id >= 0xe3f.
    const unsigned n = id < kTextIdCount && id < g_igdtTablesVw.text_layout_count
                           ? g_igdtTablesVw.text_layouts[id].max_chars
                           : 0;
    if (id < kTextIdCount) g_textStrIds[id] = kNoStrId;
    if (n == 0) return IGDT_OK;
    IgdtWchar buf[256];
    memset(buf, 0, sizeof(buf));
    for (unsigned i = 0; i < n && wstr[i] != 0; ++i) {
        const IgdtWchar c = wstr[i];
        buf[i] = (c == 1 || c == 2 || c > 0x10000) ? 3 : c;
    }
    return MarkTextDirty(IgdtSetRuntimeText(g_igdtTablesVw, id, buf));
}

int hmi_wrapper_igdt_set_text_utf8(unsigned id, const char* utf8) {
    IgdtWchar buf[256];
    IgdtUtf8ToUtf32(utf8, buf, sizeof(buf) / sizeof(buf[0]));
    return hmi_wrapper_igdt_set_text_wstr(id, buf);
}

// DEVIATION: stock copies 100 words whatever the string length (its loop
// test is on the pointer, never NULL); ours stops at the terminator.
// 0x000624f0 hmi_wrapper_igdt_set_text_tabular
int hmi_wrapper_igdt_set_text_tabular(unsigned id, const IgdtWchar* wstr) {
    if (!wstr) return 0;
    IgdtWchar buf[101];
    memset(buf, 0, sizeof(buf));
    for (unsigned i = 0; i < 100 && wstr[i] != 0; ++i) {
        const IgdtWchar c = wstr[i];
        buf[i] = (c - 0x30u < 10u) ? c + 0xf800u : c;
    }
    const int ret = hmi_wrapper_igdt_set_text_wstr(id, buf);
    return (unsigned)ret > 1u ? 0 : 1 - ret;
}

// 0x0006125c hmi_wrapper_igdt_set_lang
bool hmi_wrapper_igdt_set_lang(int lang) {
    if (lang == g_igdtLang) return false;
    IgdtLock lock;
    if (lang < 0 || lang >= kLangCount) return false;
    g_igdtLang = lang;  // 0x0031d210
    for (unsigned id = 0; id < kTextIdCount; ++id) {
        if (g_textStrIds[id] != kNoStrId) SetTextFromId(id, g_textStrIds[id]);
    }
    return true;
}

// 0x000612cc hmi_wrapper_igdt_get_lang
int hmi_wrapper_igdt_get_lang() {
    return g_igdtLang;
}

// 0x0006167c hmi_wrapper_igdt_set_branch
int hmi_wrapper_igdt_set_branch(uint32_t tag, uint16_t idx) {
    IgdtLock lock;
    return IgdtSetBranch(IgdtGetRuntime(), tag, idx);
}

// 0x000616a8 hmi_wrapper_igdt_set_state
int hmi_wrapper_igdt_set_state(uint32_t tag, uint16_t idx) {
    IgdtLock lock;
    return IgdtSetState(IgdtGetRuntime(), tag, idx);
}

// 0x00061700 hmi_wrapper_igdt_set_touch_area
int hmi_wrapper_igdt_set_touch_area(uint32_t tag, const IgdtClipRect& rect) {
    IgdtLock lock;
    return IgdtSetTouchArea(IgdtGetRuntime(), tag, rect.x, rect.y, rect.w, rect.h);
}

// 0x000619b0 hmi_wrapper_igdt_get_touch_area
int hmi_wrapper_igdt_get_touch_area(uint32_t tag, IgdtClipRect* rect) {
    IgdtLock lock;
    return IgdtGetTouchArea(IgdtGetRuntime(), tag, rect);
}

// 0x00061758 hmi_wrapper_igdt_set_color
int hmi_wrapper_igdt_set_color(uint32_t tag, uint32_t argb) {
    IgdtLock lock;
    return IgdtSetColor(IgdtGetRuntime(), tag, argb);
}

// 0x00061784 hmi_wrapper_igdt_set_alpha
int hmi_wrapper_igdt_set_alpha(uint32_t tag, uint8_t alpha) {
    IgdtLock lock;
    return IgdtSetAlpha(IgdtGetRuntime(), tag, alpha);
}

// 0x00061840 hmi_wrapper_igdt_set_x
int hmi_wrapper_igdt_set_x(uint32_t tag, int16_t x) {
    IgdtLock lock;
    return IgdtSetPosX(IgdtGetRuntime(), tag, x);
}

// 0x0006186c hmi_wrapper_igdt_set_y
int hmi_wrapper_igdt_set_y(uint32_t tag, int16_t y) {
    IgdtLock lock;
    return IgdtSetPosY(IgdtGetRuntime(), tag, y);
}

// 0x00061808 hmi_wrapper_igdt_set_xy
int hmi_wrapper_igdt_set_xy(uint32_t tag, int16_t x, int16_t y) {
    IgdtLock lock;
    return IgdtSetPosXY(IgdtGetRuntime(), tag, x, y);
}

// Writes x / y only on success.
// 0x000618c4 hmi_wrapper_igdt_get_xy
int hmi_wrapper_igdt_get_xy(uint32_t tag, int16_t* x, int16_t* y) {
    int16_t lx = 0, ly = 0;
    int ret;
    {
        IgdtLock lock;
        ret = IgdtGetPosXY(IgdtGetRuntime(), tag, &lx, &ly);
    }
    if (ret == IGDT_OK) {
        *x = lx;
        *y = ly;
    }
    return ret;
}

// 0x00061b3c hmi_wrapper_igdt_set_scale
int hmi_wrapper_igdt_set_scale(uint32_t tag, float sx, float sy) {
    IgdtLock lock;
    return IgdtSetScale(IgdtGetRuntime(), tag, sx, sy);
}

// 0x00061b10 hmi_wrapper_igdt_set_matrix
int hmi_wrapper_igdt_set_matrix(uint32_t tag, const float m[9]) {
    IgdtLock lock;
    return IgdtSetMatrix(IgdtGetRuntime(), tag, m);
}

// 0x00061a10 hmi_wrapper_igdt_reset_node
int hmi_wrapper_igdt_reset_node(uint32_t tag) {
    IgdtLock lock;
    return IgdtResetNode(IgdtGetRuntime(), g_igdtTablesVw, tag);
}

// TODO(phase3): with the flag clear, stock still permits hardkeys while
// screen 0x52 is shown in some phone / projection states (VPageManager
// 0x0002b8b8, 0x0006f768 / 0x0006f770, 0x00043b50, VPowerProxy 0x000563ec).
// 0x00062a28 hmi_wrapper_igdt_get_hardkey_permit
bool hmi_wrapper_igdt_get_hardkey_permit() {
    return g_hardkeyPermit;
}

void hmi_wrapper_igdt_set_hardkey_permit(bool on) {
    g_hardkeyPermit = on;
}

// 0x00062d50 hmi_wrapper_igdt_get_mutihardkey_permit
bool hmi_wrapper_igdt_get_mutihardkey_permit() {
    return g_mutiHardkeyPermit;
}

// 0x00062d40 hmi_wrapper_igdt_set_mutihardkey_permit
void hmi_wrapper_igdt_set_mutihardkey_permit(bool on) {
    g_mutiHardkeyPermit = on;
}

// 0x00062d70 hmi_wrapper_igdt_get_touch_permit
bool hmi_wrapper_igdt_get_touch_permit() {
    return g_touchPermit;
}

// 0x00062d60 hmi_wrapper_igdt_set_touch_permit
void hmi_wrapper_igdt_set_touch_permit(bool on) {
    g_touchPermit = on;
}

// 0x00062e60 igdtTask::igdtTask
igdtTask::igdtTask() : QObject(0), m_thread(0) {
    hmi_wrapper_igdt_init();
    moveToThread(&m_thread);
    m_thread.setStackSize(kRenderStackSize);
    m_thread.start(QThread::InheritPriority);
    m_timer = 0;
    connect(this, SIGNAL(SigInitialize()), this, SLOT(InitializeSlot()));
    emit SigInitialize();
}

// 0x00062f14 igdtTask::Instance
igdtTask* igdtTask::Instance() {
    if (!g_igdtTask) g_igdtTask = new igdtTask();
    return g_igdtTask;
}

// 0x00062f5c igdtTask::~igdtTask
igdtTask::~igdtTask() {
    delete m_timer;
    m_timer = 0;
}

// Runs on m_thread (queued SigInitialize).
// 0x0006304c igdtTask::InitializeSlot (0x00062fdc connections)
void igdtTask::InitializeSlot() {
    m_timer = new QTimer(0);
    connect(this, SIGNAL(sigReflash()), this, SLOT(OnCircleReflash()));
    connect(m_timer, SIGNAL(timeout()), this, SLOT(OnTimeOut()));
    m_timer->start(kReflashMs);
}

// DEVIATION: stock 0x00063260 only tail-calls the sigReflash emitter
// (0x003af740), which would recurse forever through the sigReflash ->
// OnCircleReflash connection; nothing in 5624 emits sigReflash. Ours is empty.
// 0x00063260 igdtTask::OnCircleReflash
void igdtTask::OnCircleReflash() {}

// TODO(phase3): stock skips the show while certain pages (VPageManager at
// 0x0002b8b8, ids 0x4f, 0xf9, 0x60, 0x6b, 0x5e, 0xfb, 0xf4, 0xf5) report
// that they are busy animating or loading.
// 0x00063084 igdtTask::OnTimeOut
void igdtTask::OnTimeOut() {
    hmi_wrapper_igdt_show();
    if (m_timer && !m_timer->isActive()) m_timer->start();
}
