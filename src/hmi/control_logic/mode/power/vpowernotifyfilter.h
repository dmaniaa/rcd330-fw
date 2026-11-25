// Stock source: control_logic/mode/power/vpowernotifyfilter.cpp
//
// First filter for VControlTask::PowersubsystemNotifyMsg messages
// (para[0] 2): aux (para[1] 3), eject (4) and audio mode (5) status.
#ifndef RCD330_HMI_CONTROL_LOGIC_MODE_POWER_VPOWERNOTIFYFILTER_H
#define RCD330_HMI_CONTROL_LOGIC_MODE_POWER_VPOWERNOTIFYFILTER_H

#include "control_logic/mode/vbasefilter.h"

class VPowerNotifyFilter : public VBaseFilter {
public:
    explicit VPowerNotifyFilter(IFilterActions* actions);

protected:
    virtual FILTER_MSG HandlePowerVOLStatus(const FILTER_MSG& msg);
    virtual FILTER_MSG HandlePowerAuxStatus(const FILTER_MSG& msg);
    virtual FILTER_MSG HandlePowerEjectStatus(const FILTER_MSG& msg);
    virtual FILTER_MSG HandleAudioModeStatus(const FILTER_MSG& msg);

private:
    bool m_logged_aux;  // ours: HandlePowerAuxStatus is phase 3, log once
};

#endif // RCD330_HMI_CONTROL_LOGIC_MODE_POWER_VPOWERNOTIFYFILTER_H
