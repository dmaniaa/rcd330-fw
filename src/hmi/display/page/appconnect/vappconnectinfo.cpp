// Stock source: display/page/appconnect/vappconnectinfo.cpp
#include "display/page/appconnect/vappconnectinfo.h"

#include <stdlib.h>

#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/display/vdisplayproxy.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "display/base/control_layout.h"
#include "display/base/controlcreator.h"
#include "display/controls/controlparts/igdtbutton.h"
#include "display/controls/controlparts/igdttextitem.h"
#include "display/igdt/extend/igdt_wrapper.h"
#include "service/osal/osal_api.h"

using STDPlusLogicalControlLibrary::IgdtButton;
using STDPlusLogicalControlLibrary::IgdtTextItem;

namespace {

const char kFile[] = "display/page/appconnect/vappconnectinfo.cpp";
const uint8_t kScreen = 0x03;

const uint32_t kTintTags[] = {0x42100268, 0x42100269, 0x4210026a};
const uint32_t kTintRegion2Platform1 = 0xffc00010;
const uint32_t kTintRegion2 = 0xff1e5aff;
const uint32_t kTintDefault = 0xffe6be00;

const unsigned kTextHeadline = 0x0f;
const unsigned kTextLine1 = 0x10;   // lines 1..9 are text ids 0x10..0x18


void LogNull(const char* what, const char* func, int line) {
    log_simple("ERROR", " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func, line);
    printlog(3, "ERROR", kFile, func, line, " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func,
             line);
}

} // namespace

// 0x00345a60 vAppConnectInfo::vAppConnectInfo
vAppConnectInfo::vAppConnectInfo() : m_txt_headline(0), m_btn_back(0) {
    uint32_t tint = kTintDefault;
    if (VAppConnectProxy::Instance()->GetRegionMode() == 2) {
        tint = VPowerProxy::Instance()->GetPlatformType() == 1 ? kTintRegion2Platform1 : kTintRegion2;
    }
    for (size_t i = 0; i < sizeof(kTintTags) / sizeof(kTintTags[0]); ++i) {
        hmi_wrapper_igdt_set_color(kTintTags[i], tint);
    }
    if (!ControlCreator::CreateComposite(this, kScreen, 0x40100163, 0, 0x3d, 800, 0xc, ICF_LCR)) {
        // Stock: __assert_fail("divider != __null", ..., 0x79).
        log_simple("ERROR", " ==== HMI ==== ERROR: divider != __null failed (%s:%d)\n", kFile, 0x79);
        abort();
    }
    m_txt_headline = new IgdtTextItem(kTextHeadline);
    AddChild(m_txt_headline);
    for (int i = 0; i < kLineCount; ++i) {
        m_txt_line[i] = new IgdtTextItem(kTextLine1 + i);
        AddChild(m_txt_line[i]);
    }
    m_btn_back = ControlCreator::CreateButton(this, kScreen, 0x4010015d, 0x2cf, 1, 0x3d, 0x3d, ICF_LCR);
    if (!m_btn_back) {
        LogNull("m_btn_back", "vAppConnectInfo", 0xaf);
    } else {
        connect(m_btn_back, SIGNAL(SigClicked(Item*, int32_t, int32_t, int32_t)), this, SLOT(OnBtnBackClicked()));
    }
}

// DEVIATION: stock NULL-checks every text item (logging m_txt_line<n>);
// ours are created with operator new in the constructor and never NULL.
// 0x003462c8 vAppConnectInfo::InitScreen
void vAppConnectInfo::InitScreen() {
    m_txt_headline->SetText(0x1c5fau);
    const int lang = VDisplayProxy::Instance()->GetLanguage();
    if (VAppConnectProxy::Instance()->GetRegionMode() == 2) {
        m_txt_line[0]->SetText(0x1f3bbu);
        m_txt_line[1]->SetText(0x1f3cfu);
        m_txt_line[2]->SetText(0x1f40eu);
        m_txt_line[3]->SetText(0x1f443u);
        // Languages 0 and 4 get a fifth line.
        if (lang == 0 || lang == 4) {
            m_txt_line[4]->SetText(0x1f456u);
        } else {
            m_txt_line[4]->SetText("");
        }
    } else {
        m_txt_line[0]->SetText(0x1c6dcu);
        m_txt_line[1]->SetText(0x1c761u);
        m_txt_line[2]->SetText(0x1c7c5u);
        m_txt_line[3]->SetText(0x1c82bu);
        if (lang == 0) {
            m_txt_line[4]->SetText(0x1c8b8u);
        } else {
            m_txt_line[4]->SetText("");
        }
    }
    for (int i = 5; i < kLineCount; ++i) m_txt_line[i]->SetText("");
}
// 0x00346f08 vAppConnectInfo::Start
void vAppConnectInfo::Start() {
    VBasePage::Start();
    InitScreen();
    hmi_wrapper_igdt_insert_page(kScreen, 0, this);
}

// 0x003462a4 vAppConnectInfo::Stop
void vAppConnectInfo::Stop() {
    VBasePage::Stop();
    hmi_wrapper_igdt_remove_page(kScreen, 0);
}

// TODO(phase3): the Back hard key (vtable +0x118, 0x00346f44) closes it too.
// 0x00346f30 vAppConnectInfo::OnBtnBackClicked
void vAppConnectInfo::OnBtnBackClicked() {
    VDisplayProxy::Instance()->RemovePage(kScreen);
}
