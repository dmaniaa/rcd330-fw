// Stock source: display/page/tuner/vtunermainpage.cpp (printlog file names).
//
// Radio main page (screen 0xf9, layer 0), the boot page. Phase 2 keeps only
// the page shell: it puts screen 0xf9 (status line included) on the stack
// so the pages above it pause and resume it. The station list, presets and
// the VTunerMainPageImpl_ logic (0x0012411c) are phase 3.
#ifndef RCD330_HMI_DISPLAY_PAGE_TUNER_VTUNERMAINPAGE_H
#define RCD330_HMI_DISPLAY_PAGE_TUNER_VTUNERMAINPAGE_H

#include "display/page/base/basepage.h"

class VTunerMainPage : public VBasePage {
    Q_OBJECT
public:
    VTunerMainPage();

    virtual void Start();
    virtual void Stop();
    virtual void Pause();
    virtual void Resume();
    // Always 0xf9 (stock 0x0011df50), also before InsertPage assigns it.
    virtual int GetScreenId() const;
};

#endif
