// Stock source: control_logic/subsystem/system_time_calendar/vsystemtimecalendarproxy.cpp
//
// Cluster / BAP clock and calendar for the status bar clock.
// RecieveVmfMsg is TODO(phase3): it logs the first message and drops all.
#ifndef RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_SYSTEM_TIME_CALENDAR_VSYSTEMTIMECALENDARPROXY_H
#define RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_SYSTEM_TIME_CALENDAR_VSYSTEMTIMECALENDARPROXY_H

#include <stdint.h>

#include "service/utility/vbaseproxy.h"

class VSystemTimeCalendarProxy : public VBaseProxy {
public:
    static VSystemTimeCalendarProxy* Instance();

    void Init();

    virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len);

private:
    VSystemTimeCalendarProxy();

    bool m_logged;
};

#endif // RCD330_HMI_CONTROL_LOGIC_SUBSYSTEM_SYSTEM_TIME_CALENDAR_VSYSTEMTIMECALENDARPROXY_H
