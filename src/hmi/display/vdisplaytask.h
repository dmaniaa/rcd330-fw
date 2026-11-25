// Stock source: display/vdisplaytask.cpp (printlog file names).
//
// VDisplayTask (singleton 0x00973fcc, moc metaobject 0x008e3ed8): the
// display thread. Every page lives and runs on it: the page requests of
// VDisplayProxy (IDisplaySink, called from any thread) are queued signals
// into OnShowPage / OnForceShowPage / OnClosePage, and touch input arrives
// queued from VMultiTouch (raw tslib) and from the igdtTask render thread
// (IGDT node touches, SigIgdtTouchEvent). Hard keys VControlTask reports
// (SigKeyEventNotify) go to the IGDT wrapper and back to VControlTask.
//
// Start-up (main 0x00010e58), after VControlTask::Init / Run:
//   new VDisplayTask; Init(); Run();
// Init creates VPageManager and igdtTask (the render thread starts here).
#ifndef RCD330_HMI_DISPLAY_VDISPLAYTASK_H
#define RCD330_HMI_DISPLAY_VDISPLAYTASK_H

#include <QtCore/QMetaType>

#include <stdint.h>

#include "control_logic/input/input_decoders.h"
#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "display/igdt/igdt_touch.h"
#include "service/utility/vbasetask.h"

class QTimer;
class VMultiTouch;

// IGDT touch carried across threads (registered as "GFX_MGR02_TOUCH_QDATA"):
// IgdtTouchEvent plus four words stock always sends as 0.
struct GFX_MGR02_TOUCH_QDATA {
    int32_t x;
    int32_t y;
    uint32_t type;
    uint32_t reserved[4];
};

class VDisplayTask : public VBaseTask, public IDisplaySink {
    Q_OBJECT
public:
    VDisplayTask();
    virtual ~VDisplayTask();
    // The object the constructor registered (NULL before).
    static VDisplayTask* Instance();

    void Init();
    // Starts the touch reader and this task's thread.
    void Run();

signals:
    // IDisplaySink: emitted from any thread, queued to the On* slots.
    void SigShowPage(unsigned char scr_id);
    void SigForceShowPage(unsigned char scr_id);
    void SigClosePage(unsigned char scr_id);
    void SigRemoveAllPage();
    void SigIgdtTouchEvent(uint tag, GFX_MGR02_TOUCH_QDATA data);

private slots:
    virtual void InitializeSlot();
    void OnShowPage(unsigned char scr_id);
    void OnForceShowPage(unsigned char scr_id);
    void OnClosePage(unsigned char scr_id);
    void OnRemoveAllPage();
    void OnDisplayTimeOut();
    void OnTouchEvent(VMULTITOUCH_DATA data);
    void OnIgdtTouchEvent(uint tag, GFX_MGR02_TOUCH_QDATA data);
    void SlotKeyEventNotify(unsigned char key, unsigned char state);

private:
    static void IgdtTouchCallback(uint32_t tag, const IgdtTouchEvent& ev, void* ctx);

    VMultiTouch* m_multitouch;  // +0x14
    QTimer* m_display_timer;    // +0x1c (20 ms)
};

#endif
