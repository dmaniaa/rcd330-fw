// Stock source: control_logic/mode/display/vdisplaycmdfilter.cpp
#include "control_logic/mode/display/vdisplaycmdfilter.h"

#include "control_logic/mode/ifilteractions.h"

namespace {

const char kFile[] = "control_logic/mode/display/vdisplaycmdfilter.cpp";

const uint8_t kCmdDisplayOps = 0x07;
const uint8_t kOpInsertPage = 2;
const uint8_t kOpClosePage = 6;

// Bytes each op takes, op byte included (0x00040e38).
int OpLength(uint8_t op) {
    switch (op) {
    case 1:
        return 3;
    case 2: case 3: case 4: case 5: case 6:
        return 2;
    case 7:
        return 1;
    default:
        return 0;
    }
}

}  // namespace

// 0x000411b8 VDisplayCmdFilter::VDisplayCmdFilter
VDisplayCmdFilter::VDisplayCmdFilter(IFilterActions* actions) : VBaseFilter(actions), m_logged_phase3_op(false) {}

// 0x00040e38 VDisplayCmdFilter::HandleCmdMsg
// TODO(phase3): para[0] 0x0c (popup 0x78), 0x0e, 0x10 (close popup 0x43) and
// 1 (BT) commands; ops 1, 3, 4, 5 and 7 (page stack / popup show calls
// 0x0004ff98, 0x0004ffa4, 0x0004ffd0, 0x00053d94, 0x0004ffec).
// DEVIATION: stock walks para[1] ops over a 0x1004-byte para and loops on an
// unknown op without advancing; here the walk stops at an unknown op or at
// the end of para.
FILTER_MSG VDisplayCmdFilter::HandleCmdMsg(const FILTER_MSG& msg) {
    FilterLog(kFile, "HandleCmdMsg", 0x4e, " ==== HMI ==== INFO: VDisplayCmdFilter::HandleCmdMsg %d %d", msg.para[0],
              msg.para[1]);
    if (msg.para[0] != kCmdDisplayOps || msg.para[1] == 0) {
        return msg;
    }
    bool page_changed = false;
    int pos = 2;
    for (int i = 0; i < msg.para[1]; ++i) {
        const uint8_t op = msg.para[pos];
        const int len = OpLength(op);
        if (len == 0 || pos + len > kFilterParaSize) {
            break;
        }
        if (op == kOpInsertPage) {
            m_actions->InsertPage(msg.para[pos + 1]);
            page_changed = true;
        } else if (op == kOpClosePage) {
            m_actions->ClosePopPage(msg.para[pos + 1]);
        } else if (!m_logged_phase3_op) {
            m_logged_phase3_op = true;
            FilterLog(kFile, "HandleCmdMsg", 0x4e, " ==== HMI ==== WARN: display op %d is phase 3; ignored (logged once)", op);
        }
        pos += len;
    }
    if (page_changed) {
        m_actions->NotifyHmiOn();
    }
    // TODO(phase3): popup ops end with VDisplayProxy 0x000500b0.
    return msg;
}
