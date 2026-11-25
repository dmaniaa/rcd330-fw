// Stock source: control_logic/mode/camera/vcameranotifyfilter.cpp
//
// Chain node only (vtable 0x003fcc78): VControlTask links it to
// VPrehandleFilter like stock 0x0002fd18, but nothing feeds it yet.
// TODO(phase3): the stock notification handlers of this filter.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_CAMERA_VCAMERANOTIFYFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_CAMERA_VCAMERANOTIFYFILTER_H

#include "control_logic/mode/vbasefilter.h"

class VCameraNotifyFilter : public VBaseFilter {
public:
    explicit VCameraNotifyFilter(IFilterActions* actions);
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_CAMERA_VCAMERANOTIFYFILTER_H
