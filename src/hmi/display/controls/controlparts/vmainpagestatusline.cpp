// Stock source: display/controls/controlparts/vmainpagestatusline.cpp
#include "display/controls/controlparts/vmainpagestatusline.h"

#include <string.h>
#include <time.h>

#include "control_logic/subsystem/power/vpowerproxy.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "service/osal/osal_api.h"

namespace {

// IGDT runtime text id of the status-bar clock.
const unsigned kClockTextId = 6;

} // namespace

// 0x002e61f0 VMainPageStatusLine::VMainPageStatusLine
VMainPageStatusLine::VMainPageStatusLine() {}

// No origin tag: ours (the formatting half of setTitleBarTime).
void VMainPageStatusLine::FormatTitleBarTime(const struct tm& t, bool valid, bool fmt12, unsigned* out,
                                             int out_len) {
    if (!out || out_len <= 0) return;
    memset(out, 0, sizeof(unsigned) * out_len);
    if (!valid) return;
    struct tm local = t;
    if (fmt12) {
        if (local.tm_hour < 13) {
            if (local.tm_hour == 0) local.tm_hour = 12;
        } else {
            local.tm_hour -= 12;
        }
    }
    char text[80];
    strftime(text, sizeof(text), "%H:%M", &local);
    // STOCK-QUIRK: a leading '0' is dropped ("09:05" -> "9:05"), in 24 h
    // mode too.
    const char* p = text[0] == '0' ? text + 1 : text;
    for (int i = 0; i < out_len - 1 && p[i] != 0; ++i) out[i] = (unsigned char)p[i];
}

// DEVIATION: an invalid time clears the text through set_text_utf8 (stock
// sets an empty QString through 0x00061b9c).
// 0x002e61f8 VMainPageStatusLine::setTitleBarTime
void VMainPageStatusLine::setTitleBarTime() {
    uint8_t f0 = 0, valid = 0, fmt12 = 0;
    VPowerProxy::Instance()->GetTimeDisplayFlags(&f0, &valid, &fmt12);
    if (valid == 0) {
        hmi_wrapper_igdt_set_text_utf8(kClockTextId, "");
        return;
    }
    struct tm t;
    memset(&t, 0, sizeof(t));
    const time_t now = time(0);
    const struct tm* lt = localtime(&now);
    if (!lt) {
        log_simple("ERROR", " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", "info", "setTitleBarTime",
                   0x26e);
        printlog(3, "ERROR", "display/controls/controlparts/vmainpagestatusline.cpp", "setTitleBarTime", 0x26e,
                 " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", "info", "setTitleBarTime", 0x26e);
    } else {
        t = *lt;
    }
    unsigned buf[100];
    FormatTitleBarTime(t, true, fmt12 == 1, buf, 100);
    hmi_wrapper_igdt_set_text_tabular(kClockTextId, buf);
}
