// Stock source: control_logic/mode/media/vmedianotifyfilter.cpp
//
// Chain node only (vtable 0x003ef040): VControlTask links it to
// VPrehandleFilter like stock 0x0002fd18, but nothing feeds it yet.
// TODO(phase3): the stock notification handlers of this filter.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_MEDIA_VMEDIANOTIFYFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_MEDIA_VMEDIANOTIFYFILTER_H

#include "control_logic/mode/vbasefilter.h"

class VMediaNotifyFilter : public VBaseFilter {
public:
    explicit VMediaNotifyFilter(IFilterActions* actions);
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_MEDIA_VMEDIANOTIFYFILTER_H
