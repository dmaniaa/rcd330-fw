// Stock path display/igdt/extend/igdt_wrapper.cpp (printlog file names).
//
// hmi_wrapper_igdt_* : the HMI's entry points into IGDT. Every call that
// touches the IGDT runtime, the runtime text or the touch queue holds the
// recursive IGDT mutex (stock 0x00a0fa40, 0x003212a0 / 0x003212ac), so page
// code (Qt main thread), the input drivers and the igdtTask render thread
// can call them concurrently. Setters return IGDT_OK or the stock error code
// of the IGDT call they wrap (igdt_runtime.h, igdt_textbuf.h).
#ifndef RCD330_HMI_DISPLAY_IGDT_EXTEND_IGDT_WRAPPER_H
#define RCD330_HMI_DISPLAY_IGDT_EXTEND_IGDT_WRAPPER_H

#include <QtCore/QObject>
#include <QtCore/QThread>

#include <stdint.h>

#include "display/igdt/igdt_tables.h"
#include "display/igdt/igdt_textbuf.h"
#include "display/igdt/igdt_touch.h"

class QTimer;

// 0x00060e3c: IGDT cold init (mutex, runtime, touch queue), language 1, no
// text ids recorded.
void hmi_wrapper_igdt_init();

// 0x00060f54: the first call brings up EGL/GLES2 (0x0031d60c -> 0x00326084)
// and FreeType; every call then drains the touch queue (callbacks run on the
// calling thread) and, when the runtime changed, redraws every layer and
// swaps.
void hmi_wrapper_igdt_show();

// True once hmi_wrapper_igdt_show brought up EGL/GLES2 successfully.
// DEVIATION: stock has no such query (it ignores 0x00326084's result).
bool hmi_wrapper_igdt_gl_ready();

// 0x000610a8 / 0x000611bc: VPageManager::InsertPage (page = the VBasePage
// on screen_id's layer, which shows the screen) / VPageManager::RemovePage
// plus hiding the screen. Inserting while a touch is down cancels a capture
// on that screen (any screen for 0xff) with IGDT_TOUCH_CANCEL_PAGE.
void hmi_wrapper_igdt_insert_page(uint8_t screen_id, int mode, void* page);
void hmi_wrapper_igdt_remove_page(uint8_t screen_id, int mode);
// The IGDT half of VPageManager::ShowIgdtScreen: shows screen_id on its
// layer and sets its page flag. Name is ours.
void hmi_wrapper_igdt_show_screen(uint8_t screen_id);
// 0x00061220: page flag of screen_id (set by show_screen, cleared by remove).
bool hmi_wrapper_igdt_is_page_shown(uint8_t screen_id);

// 0x000612d4: raw tslib-style event types 3 press, 4 move, 5 release (others
// are ignored). Press clears and release sets the hardkey permit; events are
// queued only while the touch permit is set.
void hmi_wrapper_igdt_touch_event(int32_t x, int32_t y, int raw_type);

// Receiver of every touch event that reaches a node (stock: 0x00063298 into
// the page manager). Name is ours.
void hmi_wrapper_igdt_set_touch_callback(IgdtTouchCallback cb, void* ctx);

// Stock 0x00062b84 (VDisplayTask::SlotKeyEventNotify): hard key `key`
// (1..10), state 1 press / 0 release / 3 repeat. Not consumed by an IGDT
// screen, it is re-posted to VControlTask::KeySubsystemNotifyMsg.
void hmi_wrapper_igdt_post_button_event(uint8_t key, uint8_t state);
// Forgets the held hard key if it is `key` (after a re-post, or from a page).
bool hmi_wrapper_igdt_release_key(uint8_t key);

// Runtime text of text id `id` (text_layout index below 0xe3f). set_text_id
// records str_id so hmi_wrapper_igdt_set_lang re-translates it; the others
// forget a recorded id.
int hmi_wrapper_igdt_set_text_id(unsigned id, uint32_t str_id);       // 0x000615ec
int hmi_wrapper_igdt_set_text_wstr(unsigned id, const IgdtWchar* wstr); // 0x00061510
int hmi_wrapper_igdt_set_text_utf8(unsigned id, const char* utf8);      // name is ours
// set_text_wstr with the digits '0'..'9' mapped to U+F830..U+F839 (the
// VWThesis tabular digit glyphs), e.g. the status-bar clock. Returns 1 when
// the text was set, else 0.
int hmi_wrapper_igdt_set_text_tabular(unsigned id, const IgdtWchar* wstr); // 0x000624f0, name is ours
// 0x0006125c: true when lang (< 5) differs from the current one and was set.
bool hmi_wrapper_igdt_set_lang(int lang);
int hmi_wrapper_igdt_get_lang();                                        // 0x000612cc

int hmi_wrapper_igdt_set_branch(uint32_t tag, uint16_t idx);            // 0x0006167c
int hmi_wrapper_igdt_set_state(uint32_t tag, uint16_t idx);             // 0x000616a8
int hmi_wrapper_igdt_set_touch_area(uint32_t tag, const IgdtClipRect& rect); // 0x00061700
int hmi_wrapper_igdt_get_touch_area(uint32_t tag, IgdtClipRect* rect);  // 0x000619b0
int hmi_wrapper_igdt_set_color(uint32_t tag, uint32_t argb);            // 0x00061758
int hmi_wrapper_igdt_set_alpha(uint32_t tag, uint8_t alpha);            // 0x00061784
int hmi_wrapper_igdt_set_x(uint32_t tag, int16_t x);                    // 0x00061840
int hmi_wrapper_igdt_set_y(uint32_t tag, int16_t y);                    // 0x0006186c
int hmi_wrapper_igdt_set_xy(uint32_t tag, int16_t x, int16_t y);        // 0x00061808
int hmi_wrapper_igdt_get_xy(uint32_t tag, int16_t* x, int16_t* y);      // 0x000618c4
int hmi_wrapper_igdt_set_scale(uint32_t tag, float sx, float sy);       // 0x00061b3c
int hmi_wrapper_igdt_set_matrix(uint32_t tag, const float m[9]);        // 0x00061b10
int hmi_wrapper_igdt_reset_node(uint32_t tag);                          // 0x00061a10

// Permit flags in stock .data, all initially true:
// - b_hardkey_permit (0x00969968): false while a touch is down.
// - b_touch_permit (0x00969969): touch events are queued.
// - the multi-hardkey permit (0x0096996a).
bool hmi_wrapper_igdt_get_hardkey_permit();         // 0x00062a28
void hmi_wrapper_igdt_set_hardkey_permit(bool on);  // name is ours (stock writes it inline)
bool hmi_wrapper_igdt_get_mutihardkey_permit();     // 0x00062d50
void hmi_wrapper_igdt_set_mutihardkey_permit(bool on);  // 0x00062d40
bool hmi_wrapper_igdt_get_touch_permit();           // 0x00062d70
void hmi_wrapper_igdt_set_touch_permit(bool on);    // 0x00062d60

// Stock class igdtTask (moc class name at 0x008e3d90): the IGDT render
// thread. Its constructor runs hmi_wrapper_igdt_init, moves the object onto
// its own QThread and starts it; a 32 ms QTimer there calls
// hmi_wrapper_igdt_show, so all GL work happens on that thread.
class igdtTask : public QObject {
    Q_OBJECT
public:
    static igdtTask* Instance();
    virtual ~igdtTask();

signals:
    void SigInitialize();
    void sigReflash();

private slots:
    void InitializeSlot();
    void OnCircleReflash();
    void OnTimeOut();

private:
    igdtTask();

    QThread m_thread;   // +0x08
    QTimer* m_timer;    // +0x10
};

// The phase 2 plan's name for igdtTask. Name is ours.
typedef igdtTask VReflash;

#endif
