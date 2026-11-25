// Stock source: display/page/appconnect/vappnoconnection.cpp
#include "display/page/appconnect/vappnoconnection.h"

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

const char kFile[] = "display/page/appconnect/vappnoconnection.cpp";
const uint8_t kScreen = 0x04;
const uint8_t kScrAppConnectInfo = 0x03;

// Brand tint of the App-Connect icons.
const uint32_t kTintTags[] = {0x42100284, 0x42100285, 0x42100286};
const uint32_t kTintRegion2Platform1 = 0xffc00010;
const uint32_t kTintRegion2 = 0xff1e5aff;
const uint32_t kTintDefault = 0xffe6be00;

const uint32_t kTagInfoTextY = 0x45100058;  // text block moved for 1 / 2 lines
const uint32_t kBranchApps = 0x1000043;     // 0 two rows, 1 two icons, 2 one icon, 3 none
const uint32_t kStateRow1 = 0x30000ba;
const uint32_t kStateRow2 = 0x30000bb;
const uint32_t kStateIcon1 = 0x30000bc;
const uint32_t kStateIcon2 = 0x30000bd;
const uint32_t kStateSingle = 0x30000be;

void LogNull(const char* what, const char* func, int line) {
    log_simple("ERROR", " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func, line);
    printlog(3, "ERROR", kFile, func, line, " ==== HMI ==== ERROR: The pointer: %s == NULL !! (%s ->: %d)", what, func,
             line);
}

} // namespace

// 0x0033a660 vAppNoConnection::vAppNoConnection
vAppNoConnection::vAppNoConnection()
    : m_info_txt(0), m_btn_info(0), m_info_txt_l2(0), m_app_cl(0), m_app_ml(0), m_app_cp(0), m_app_aa(0) {
    VAppConnectProxy* app = VAppConnectProxy::Instance();
    uint32_t tint = kTintDefault;
    if (app->GetRegionMode() == 2) {
        tint = VPowerProxy::Instance()->GetPlatformType() == 1 ? kTintRegion2Platform1 : kTintRegion2;
    }
    for (size_t i = 0; i < sizeof(kTintTags) / sizeof(kTintTags[0]); ++i) {
        hmi_wrapper_igdt_set_color(kTintTags[i], tint);
    }
    ControlCreator::CreateComposite(this, kScreen, 0x40100164, 0, 0x182, 800, 0x5e, ICF_9_PARTS);
    ControlCreator::CreateComposite(this, kScreen, 0x40100165, 0, 0x182, 800, 0x5e, ICF_LCR);
    m_info_txt = new IgdtTextItem(0x1a);
    AddChild(m_info_txt);
    m_info_txt_l2 = new IgdtTextItem(0x19);
    AddChild(m_info_txt_l2);
    if (app->GetRegionMode() != 0) {
        m_btn_info = ControlCreator::CreateButton(this, kScreen, 0x40100166, 0, 0x18c, 800, 0x54, ICF_9_PARTS);
        if (!m_btn_info) {
            LogNull("VBtnInfo", "vAppNoConnection", 0xbe);
        } else {
            IgdtTextItem* label = m_btn_info->GetTextItem(0);
            if (label) label->SetGlowIds(0x4210022f, 0x42100230, 0x42100231);
            connect(m_btn_info, SIGNAL(SigClicked(Item*, int32_t, int32_t, int32_t)), this, SLOT(OnBtnInfoClicked()));
            m_btn_info->SetText(0, 0x1c8d7);
        }
    }
}

// 0x0033aa60 vAppNoConnection::InitScreen
void vAppNoConnection::InitScreen() {
    VAppConnectProxy* app = VAppConnectProxy::Instance();
    if (!app->IsCoded()) {
        hmi_wrapper_igdt_set_y(kTagInfoTextY, 0xc1);
        if (m_info_txt) m_info_txt->SetText(0x1f460u);
    } else {
        hmi_wrapper_igdt_set_y(kTagInfoTextY, 0x71);
        if (!m_info_txt) {
            LogNull("VInfoTxt", "InitScreen", 0x149);
        } else {
            m_info_txt->SetText(0x1c8fbu);
        }
        if (!m_info_txt_l2) {
            LogNull("VInfoTxt_L2", "InitScreen", 0x14a);
        } else {
            m_info_txt_l2->SetText(0x1c992u);
        }
    }
    app->GetAvailableApps(&m_app_cl, &m_app_cp, &m_app_ml);
    app->GetFMAvailableAppRow(&m_app_aa, &m_app_cp, &m_app_ml);

    if (app->GetRegionMode() == 2) {
        // Android Auto / MirrorLink / CarPlay.
        const unsigned n = m_app_aa + m_app_ml + m_app_cp;
        if (n == 0) {
            hmi_wrapper_igdt_set_branch(kBranchApps, 3);
        } else if (n == 1) {
            if (m_app_aa) {
                hmi_wrapper_igdt_set_state(kStateSingle, 3);
            } else if (m_app_ml) {
                hmi_wrapper_igdt_set_state(kStateSingle, 2);
            } else if (m_app_cp) {
                hmi_wrapper_igdt_set_state(kStateSingle, 1);
            }
            hmi_wrapper_igdt_set_branch(kBranchApps, 2);
        } else if (n == 2) {
            if (!m_app_aa) {
                hmi_wrapper_igdt_set_state(kStateIcon1, 1);
                hmi_wrapper_igdt_set_state(kStateIcon2, 1);
            } else {
                hmi_wrapper_igdt_set_state(kStateIcon1, 2);
                hmi_wrapper_igdt_set_state(kStateIcon2, m_app_cp ? 2 : 1);
            }
            hmi_wrapper_igdt_set_branch(kBranchApps, 1);
        } else {
            hmi_wrapper_igdt_set_branch(kBranchApps, 0);
            hmi_wrapper_igdt_set_state(kStateRow1, 1);
            hmi_wrapper_igdt_set_state(kStateRow2, 1);
        }
        return;
    }
    // CarLife / MirrorLink / CarPlay.
    const unsigned n = m_app_cl + m_app_ml + m_app_cp;
    if (n == 0) {
        hmi_wrapper_igdt_set_branch(kBranchApps, 3);
    } else if (n == 1) {
        if (m_app_cl) {
            hmi_wrapper_igdt_set_state(kStateSingle, 0);
        } else if (m_app_ml) {
            hmi_wrapper_igdt_set_state(kStateSingle, 2);
        } else if (m_app_cp) {
            hmi_wrapper_igdt_set_state(kStateSingle, 1);
        }
        hmi_wrapper_igdt_set_branch(kBranchApps, 2);
    } else if (n == 2) {
        if (!m_app_cp) {
            hmi_wrapper_igdt_set_state(kStateIcon1, 0);
            hmi_wrapper_igdt_set_state(kStateIcon2, 1);
        } else {
            hmi_wrapper_igdt_set_state(kStateIcon1, 1);
            hmi_wrapper_igdt_set_state(kStateIcon2, m_app_cl ? 0 : 1);
        }
        hmi_wrapper_igdt_set_branch(kBranchApps, 1);
    } else {
        hmi_wrapper_igdt_set_branch(kBranchApps, 0);
        hmi_wrapper_igdt_set_state(kStateRow1, 0);
        hmi_wrapper_igdt_set_state(kStateRow2, 0);
    }
}

// 0x0033ae48 vAppNoConnection::Start
void vAppNoConnection::Start() {
    VBasePage::Start();
    InitScreen();
    hmi_wrapper_igdt_insert_page(kScreen, 0, this);
}

// 0x0033aa40 vAppNoConnection::Stop
void vAppNoConnection::Stop() {
    VBasePage::Stop();
    hmi_wrapper_igdt_remove_page(kScreen, 0);
}

// 0x0033ae70 vAppNoConnection::OnBtnInfoClicked
void vAppNoConnection::OnBtnInfoClicked() {
    VDisplayProxy::Instance()->PushPage(kScrAppConnectInfo);
}
