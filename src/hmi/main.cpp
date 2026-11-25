// Stock opt/hmi entry point (firmware 5624), recreated from
// docs/decompiled/5624/hmi/functions/0x10e58.c.
//
// Start-up order as stock: VControlTask (proxies, VMF groups, filters),
// VDisplayTask (page manager, IGDT render thread, touch), the key and knob
// drivers, then the VMF reader thread and the wake-state / first-data
// requests. The camera, fb-camera and MirrorLink tasks are phase 3.
// log_simple/printlog line numbers are the stock ones.
#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QTextCodec>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "control_logic/input/vkeydriver.h"
#include "control_logic/input/vknobdriver.h"
#include "control_logic/subsystem/amfm/vamfmproxy.h"
#include "control_logic/subsystem/appconnect/vappconnectproxy.h"
#include "control_logic/subsystem/audio/vaudioproxy.h"
#include "control_logic/subsystem/bap/vbapproxy.h"
#include "control_logic/subsystem/media/vmediaproxy.h"
#include "control_logic/subsystem/power/vpowerproxy.h"
#include "control_logic/vcontroltask.h"
#include "display/vdisplaytask.h"
#include "service/osal/osal_api.h"
#include "service/vmf/VmfThread.h"

// 0x009704c8 g_hmiAutotestEnabled
// Set when /pss1/hmi_autotest.ini enables AUTOTEST. Name is ours.
bool g_hmiAutotestEnabled = false;

namespace {

// Clears FB_ACTIVATE_FORCE (0x80, bit 7 of activate; stock var +0x54) on
// /dev/fb0 so the boot logo is not blanked. Returns true when the screen info was written back.
// 0x00023514 logo_black_avoid_handle
bool logo_black_avoid_handle() {
    const int fd = open("/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        log_simple("INFO", " ==== HMI ==== INFO: Unable to open /dev/fb0\n");
        printlog(3, "INFO", "main.cpp", "logo_black_avoid_handle", 0x54, " ==== HMI ==== INFO: Unable to open /dev/fb0\n");
        return false;
    }
    struct fb_var_screeninfo var;   // stock 0x00970428
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        close(fd);
        return false;
    }
    var.activate &= ~0x80u;
    const bool ok = ioctl(fd, FBIOPUT_VSCREENINFO, &var) >= 0;
    close(fd);
    return ok;
}

} // namespace

// 0x00010e58 main
int main(int argc, char **argv)
{
    log_simple("INFO", " ==== HMI ==== INFO: [HMI_START]HMI main start\n");
    printlog(3, "INFO", "main.cpp", "main", 0x73, " ==== HMI ==== INFO: [HMI_START]HMI main start\n");

    logo_black_avoid_handle();

    if (ini_getl("AUTOTEST", "ENABLE", 0, "/pss1/hmi_autotest.ini") == 1) {
        g_hmiAutotestEnabled = true;
    }

    // TODO(phase3): VCameraTask A (0x000de600 ctor, 0x000de754).

    const int rvc = static_cast<int>(ini_getl("CODING", "CODINGS", -1, "/pss1/stdplus.ini")) >> 1;
    log_simple("INFO", " ==== HMI ==== INFO: main RVC===============rvc coding check=====value=%d===\n", rvc);
    printlog(3, "INFO", "main.cpp", "main", 0x82,
             " ==== HMI ==== INFO: main RVC===============rvc coding check=====value=%d===\n", rvc);
    log_simple("HMI_RVC",
               " ==== HMI ==== HMI_RVC: [HMI_START] RVC===============rvc coding check=====value=%d===\n", rvc);
    printlog(3, "HMI_RVC", "main.cpp", "main", 0x83,
             " ==== HMI ==== HMI_RVC: [HMI_START] RVC===============rvc coding check=====value=%d===\n", rvc);
    if (rvc == 0) {
        log_simple("INFO", " ==== HMI ==== INFO: main RVC===============rvc coding off========\n");
        printlog(3, "INFO", "main.cpp", "main", 0x86,
                 " ==== HMI ==== INFO: main RVC===============rvc coding off========\n");
    } else {
        // TODO(phase3): DAT_009704c9 = (VCameraTask 0x000de7e0 == 0).
    }

    QCoreApplication app(argc, argv);
    QTextCodec *codec = QTextCodec::codecForName(QByteArray("utf8"));
    QTextCodec::setCodecForTr(codec);
    QTextCodec::setCodecForCStrings(codec);
    QTextCodec::setCodecForLocale(codec);
    // TODO(phase3): VCameraTask A 0x000de73c and its task start.

    // DEVIATION: stock keeps the tasks as stack objects of main(); ours are
    // heap objects that live until the process exits.
    // Order is load-bearing: VControlTask::Init creates every proxy singleton
    // on this (main) thread, Inits them and registers their VMF groups. It
    // must run before VDisplayTask::Init starts the igdtTask render thread
    // (VDisplayProxy::Init clears the igdt-shown flag the first frame sets),
    // before the key / knob driver threads and before VmfThread::start().
    VControlTask* control = new VControlTask();
    control->Init();
    control->Run();
    if (rvc == 1) {
        VPowerProxy::Instance()->SetRvcCoded(1);
    }

    VDisplayTask* display = new VDisplayTask();
    display->Init();
    display->Run();

    // TODO(phase3): fb camera 0x0004bdb4 / 0x0004c798 and its task start,
    // the VMLC tasks D..H (0x000e08f4, 0x000e0548, 0x000ea694, 0x000eb760,
    // 0x000eba70).

    VKeyDriver* keys = new VKeyDriver();
    keys->Init();
    keys->Start();
    VKnobDriver* knob = new VKnobDriver();
    knob->Init();
    knob->Run();

    // DEVIATION: stock (main 0x00010e58) calls SetStartupDone(1) after
    // VmfThread::start(). A PA_OUT_MODE_CHG RUN dispatched in between finds
    // startup state 0, sets VPowerProxy's hmi-run flag and stops at "no
    // audio mode"; the RUN that answers the 0x81 audio mode is then dropped
    // as a repeat (running_init only becomes 2 in OnSaveAudioModeToVIP
    // 0x00047810, after a first page), so no page, no frame and no wake
    // state 2 ever follow. Setting it first drops that early RUN instead
    // (startup state 1 = not started), and writing it before the VMF thread
    // exists also orders it before every VMF-thread read (thread start is a
    // happens-before edge). Pinned by test_proxies test_power_run_boot_order.
    VPowerProxy* power = VPowerProxy::Instance();
    power->SetStartupDone(1);
    VmfThread::Instance()->start();

    power->SendWakeStateToPowerAgent(1);
    VAMFMProxy* amfm = VAMFMProxy::Instance();
    amfm->OnGetCurrentInfoOpera();
    amfm->OnGetTunerListOpera();
    amfm->OnGetSeekmodeOpera();
    VMediaProxy::Instance()->OnGetMsg(0);
    VBAPProxy::Instance()->RequestSensorInfo();
    VAudioProxy::Instance()->OnGetAllAudioSetting();
    VAppConnectProxy::Instance()->QueryPssCoding();

    log_simple("INFO", " ==== HMI ==== INFO: HMI main end===================================");
    printlog(3, "INFO", "main.cpp", "main", 0xee,
             " ==== HMI ==== INFO: HMI main end===================================");

    return app.exec();
}
