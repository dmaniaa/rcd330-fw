// Stock source: display/controls/controlparts/vmainpagestatusline.cpp
//
// The status bar ("title bar") over every page: VBasePage creates one
// (VBasePage +0x60, m_statusline) and VBasePage::UpdateHeadlineDisplay
// (VA 0x0002ad60, basepage.cpp) calls setTitleBarTime every 20th tick.
// The other status-bar items (exterior temperature text 8, mute branch
// tag 0x01000000, phone/BT icons 0x01000001..3, states 0x03000000/1,
// branches 0x01000004/5) are driven by UpdateHeadlineDisplay itself, not
// by this class.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_VMAINPAGESTATUSLINE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_VMAINPAGESTATUSLINE_H

// Class name is not in the stock binary's RTTI; the file name is.
class VMainPageStatusLine {
public:
    VMainPageStatusLine();

    // Writes the clock (HH:MM, 12 h when the power proxy's 12 h flag is 1,
    // no leading zero, tabular digits) to IGDT text 6, or clears it when
    // the time is not valid yet.
    void setTitleBarTime();

    // Ours: the clock text for a given time and the two power-proxy flags,
    // as UTF-32 (at most 99 characters + 0). Empty when !valid.
    static void FormatTitleBarTime(const struct tm& t, bool valid, bool fmt12, unsigned* out, int out_len);
};

// The Phase 2 plan's name for this class.
typedef VMainPageStatusLine VStatusLine;

#endif
