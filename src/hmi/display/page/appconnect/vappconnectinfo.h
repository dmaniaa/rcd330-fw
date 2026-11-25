// Stock source: display/page/appconnect/vappconnectinfo.cpp (printlog file
// names).
//
// App-Connect information text (screen 0x03, layer 2), opened by the Info
// button of vAppNoConnection and closed by its Back button.
#ifndef RCD330_HMI_DISPLAY_PAGE_APPCONNECT_VAPPCONNECTINFO_H
#define RCD330_HMI_DISPLAY_PAGE_APPCONNECT_VAPPCONNECTINFO_H

#include "display/page/base/basepage.h"

namespace STDPlusLogicalControlLibrary {
class IgdtButton;
class IgdtTextItem;
} // namespace STDPlusLogicalControlLibrary

class vAppConnectInfo : public VBasePage {
    Q_OBJECT
public:
    vAppConnectInfo();

    virtual void Start();
    virtual void Stop();

private slots:
    void OnBtnBackClicked();

private:
    enum { kLineCount = 9 };

    void InitScreen();

    STDPlusLogicalControlLibrary::IgdtTextItem* m_txt_headline;          // +0x68
    STDPlusLogicalControlLibrary::IgdtButton* m_btn_back;                // +0x6c
    STDPlusLogicalControlLibrary::IgdtTextItem* m_txt_line[kLineCount];  // +0x70..+0x90
};

#endif
