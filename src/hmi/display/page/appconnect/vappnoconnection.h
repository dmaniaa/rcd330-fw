// Stock source: display/page/appconnect/vappnoconnection.cpp (printlog file
// names).
//
// App-Connect without a phone (screen 0x04, layer 1): the welcome text and
// the coded-app icons, plus an Info button (screen 0x03) in regions 1 / 2.
#ifndef RCD330_HMI_DISPLAY_PAGE_APPCONNECT_VAPPNOCONNECTION_H
#define RCD330_HMI_DISPLAY_PAGE_APPCONNECT_VAPPNOCONNECTION_H

#include <stdint.h>

#include "display/page/base/basepage.h"

namespace STDPlusLogicalControlLibrary {
class IgdtButton;
class IgdtTextItem;
} // namespace STDPlusLogicalControlLibrary

class vAppNoConnection : public VBasePage {
    Q_OBJECT
public:
    vAppNoConnection();

    virtual void Start();
    virtual void Stop();

private slots:
    void OnBtnInfoClicked();

private:
    void InitScreen();

    STDPlusLogicalControlLibrary::IgdtTextItem* m_info_txt;     // +0x6c VInfoTxt
    STDPlusLogicalControlLibrary::IgdtButton* m_btn_info;       // +0x70 VBtnInfo
    STDPlusLogicalControlLibrary::IgdtTextItem* m_info_txt_l2;  // +0x74 VInfoTxt_L2
    // Coded apps (VAppConnectProxy::GetAvailableApps / GetFMAvailableAppRow).
    uint8_t m_app_cl;   // +0x68 CarLife (region 1)
    uint8_t m_app_ml;   // +0x69 MirrorLink
    uint8_t m_app_cp;   // +0x6a CarPlay
    uint8_t m_app_aa;   // +0x6b Android Auto (region 2)
};

#endif
