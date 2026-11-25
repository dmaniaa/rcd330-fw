// File name is ours; stock name unknown (typeinfo "16VPrehandleFilter").
//
// Hub of the stock chain (VControlTask ctor 0x0002fd18): every notify filter
// forwards to it, and it forwards to the command filters. Its vtable
// (0x003ef428) overrides no base slot, so it passes messages through.
// TODO(phase3): three extra virtuals (0x00045e0c, 0x00045dbc, 0x00045d6c)
// that drop messages unless VPowerProxy +0x1c == 2; stock has no caller
// found for them.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_VPREHANDLEFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_VPREHANDLEFILTER_H

#include "control_logic/mode/vbasefilter.h"

class VPrehandleFilter : public VBaseFilter {
public:
    explicit VPrehandleFilter(IFilterActions* actions);
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_VPREHANDLEFILTER_H
