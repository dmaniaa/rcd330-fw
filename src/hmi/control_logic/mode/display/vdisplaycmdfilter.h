// Stock source: control_logic/mode/display/vdisplaycmdfilter.cpp
//
// Command filter behind VPrehandleFilter: display commands (para[0] 7, an
// op list) from the notify filters.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_DISPLAY_VDISPLAYCMDFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_DISPLAY_VDISPLAYCMDFILTER_H

#include "control_logic/mode/vbasefilter.h"

class VDisplayCmdFilter : public VBaseFilter {
public:
    explicit VDisplayCmdFilter(IFilterActions* actions);

protected:
    virtual FILTER_MSG HandleCmdMsg(const FILTER_MSG& msg);

private:
    bool m_logged_phase3_op;  // ours
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_DISPLAY_VDISPLAYCMDFILTER_H
