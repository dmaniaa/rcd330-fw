// Stock source: display/page/tuner/vtunermainpage.cpp
#include "display/page/tuner/vtunermainpage.h"

namespace {

const int kScreen = 0xf9;

} // namespace

// TODO(phase3): m_pImpl_ = new VTunerMainPageImpl_(this) (0x0012411c) and
// the +0x6c flag.
// 0x0012218c VTunerMainPage::VTunerMainPage
VTunerMainPage::VTunerMainPage() {}

// Stock 0x0011df50 (no decompile export: returns 0xf9).
int VTunerMainPage::GetScreenId() const {
    return kScreen;
}

// TODO(phase3): the impl's station / RDS refresh (0x00199144 tuner
// state, 0x0011f43c, 0x001232e4, 0x0012226c) and VAMFMProxy 0x000af364(1).
// 0x001201b4 VTunerMainPage::Start
void VTunerMainPage::Start() {
    VBasePage::Start();
}

// TODO(phase3): stops the impl's three timers and VAMFMProxy 0x000af364(0).
// 0x0011df58 VTunerMainPage::Stop
void VTunerMainPage::Stop() {
    VBasePage::Stop();
}

// TODO(phase3): unless a page above is a popup (0x0002bd1c), stock calls
// VAMFMProxy 0x000af364(0) and the impl's list refresh (0x0011e434 /
// 0x0011e280).
// 0x0011e500 VTunerMainPage::Pause
void VTunerMainPage::Pause() {
    VBasePage::Pause();
}

// TODO(phase3): VAMFMProxy 0x000af364(1) and the impl refresh as in Start.
// 0x0011ffdc VTunerMainPage::Resume
void VTunerMainPage::Resume() {
    VBasePage::Resume();
}
