// Stock source: control_logic/mode/audio/vaudionotifyfilter.cpp
//
// Chain node only (vtable 0x003ee0e0): VControlTask links it to
// VPrehandleFilter like stock 0x0002fd18, but nothing feeds it yet.
// TODO(phase3): the stock notification handlers of this filter.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_AUDIO_VAUDIONOTIFYFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_AUDIO_VAUDIONOTIFYFILTER_H

#include "control_logic/mode/vbasefilter.h"

class VAudioNotifyFilter : public VBaseFilter {
public:
    explicit VAudioNotifyFilter(IFilterActions* actions);
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_AUDIO_VAUDIONOTIFYFILTER_H
