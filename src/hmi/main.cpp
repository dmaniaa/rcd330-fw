// Stock opt/hmi entry point (firmware 5624), recreated from
// docs/decompiled/hmi/functions/0x10e58.c.
//
// Phase 1 keeps main()'s prologue (start banner, autotest and RVC coding
// reads, QCoreApplication with UTF-8 codecs) and its event loop. Every other
// stock call stays in place as an ordered TODO(phase2) placeholder, so phase 2
// can fill them 1:1. log_simple/printlog line numbers are the stock ones.
#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QTextCodec>

#include <stdlib.h>

#include "display/igdt/extend/igdt_wrapper.h"
#include "display/igdt/igdt_gl.h"
#include "display/igdt/igdt_scene.h"
#include "display/igdt/igdt_strings.h"
#include "display/igdt/igdt_text.h"
#include "display/igdt/igdt_textbuf.h"
#include "service/osal/osal_api.h"

// 0x009704c8 g_hmiAutotestEnabled
// Set when /pss1/hmi_autotest.ini enables AUTOTEST. Name is ours.
bool g_hmiAutotestEnabled = false;

namespace {

const uint16_t kScreenAppNoConn = 0x04;

// DEVIATION: dev hook stands in for the phase-3 page. Writes the text that
// vAppNoConnection (display/page/appconnect/vappnoconnection.cpp) puts on
// screen 0x04 with App-Connect coded (FUN_00347224 != 0, as in QEMU), in
// stock order: the VBtnInfo label from the constructor (0x0033a660), then
// VInfoTxt and VInfoTxt_L2 from InitScreen (0x0033aa60). Language en_US,
// the stock default. Widget text ids are text_layout indices.
void DevFillAppNoConnText() {
    const struct {
        unsigned id;
        uint32_t str_id;
    } texts[] = {
        {0x1c, 0x1c8d7},   // VBtnInfo: "Info"
        {0x1a, 0x1c8fb},   // VInfoTxt: "Welcome to App-Connect."
        {0x19, 0x1c992},   // VInfoTxt_L2: "Please connect a device using the USB."
    };
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i) {
        IgdtSetRuntimeTextId(g_igdtTablesVw, g_igdtStringsVw, texts[i].id, texts[i].str_id, IGDT_LANG_EN_US);
    }
}

// DEVIATION: dev-only until phase 2 PageManager. HMI_DEV_SCREEN=<id> (e.g.
// 0x04) draws that screen once after GL init, so the renderer can be
// compared with stock (tools/tests/test_hmi_parity.py). For 0x04 the
// runtime text comes from DevFillAppNoConnText; other screens show only
// kind-4 (engineering) text.
bool DevDrawScreen() {
    const char* env = getenv("HMI_DEV_SCREEN");
    if (!env || !*env) return false;
    char* end = 0;
    const unsigned long id = strtoul(env, &end, 0);
    if (*end != '\0' || id > 0xffff) {
        log_simple("INFO", " ==== HMI ==== INFO: HMI_DEV_SCREEN=%s is not a screen id\n", env);
        return false;
    }
    hmi_wrapper_igdt_init();
    hmi_wrapper_igdt_show();
    if (!hmi_wrapper_igdt_gl_ready()) {
        log_simple("INFO", " ==== HMI ==== INFO: dev screen 0x%02lx skipped: no GL\n", id);
        return false;
    }
    if (id == kScreenAppNoConn) DevFillAppNoConnText();
    if (!IgdtSceneDraw(g_igdtTablesVw, (uint16_t)id)) return true;
    IgdtSwap();
    log_simple("INFO", " ==== HMI ==== INFO: dev screen 0x%02lx drawn\n", id);
    return true;
}

} // namespace

// 0x00010e58 main
int main(int argc, char **argv)
{
    log_simple("INFO", " ==== HMI ==== INFO: [HMI_START]HMI main start\n");
    printlog(3, "INFO", "main.cpp", "main", 0x73, " ==== HMI ==== INFO: [HMI_START]HMI main start\n");

    // TODO(phase2): 0x00023514 logo_black_avoid_handle() (/dev/fb0 FBIOGET/PUT_VSCREENINFO)

    if (ini_getl("AUTOTEST", "ENABLE", 0, "/pss1/hmi_autotest.ini") == 1) {
        g_hmiAutotestEnabled = true;
    }

    // TODO(phase2): 0x000de600 ctor of stack object A (auStack_a4)
    // TODO(phase2): 0x000de754 A.method()

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
        // TODO(phase2): DAT_009704c9 = (0x000de7e0 A.method() == 0)
    }

    QCoreApplication app(argc, argv);
    QTextCodec *codec = QTextCodec::codecForName(QByteArray("utf8"));
    QTextCodec::setCodecForTr(codec);
    QTextCodec::setCodecForCStrings(codec);
    QTextCodec::setCodecForLocale(codec);

    // TODO(phase2): 0x000de73c A.method()
    // TODO(phase2): 0x00060ac8 A.start() (thunk)
    // TODO(phase2): 0x0002fd18 ctor of stack object B (auStack_230)
    // TODO(phase2): 0x00033c68 B.method()
    // TODO(phase2): 0x00030064 B.method()
    // TODO(phase2): if (rvc == 1) 0x0005a394(0x00055fd4 instance(), 1)
    // TODO(phase2): 0x00063344 ctor of stack object C (auStack_108)
    // TODO(phase2): 0x00065238 C.method()
    // TODO(phase2): 0x00063584 C.method()
    // TODO(phase2): 0x0004bdb4 instance()
    // TODO(phase2): 0x0004c798()
    // TODO(phase2): 0x0004bdb4 instance()
    // TODO(phase2): 0x00060ac8 start() (thunk)
    // TODO(phase2): 0x000e08f4 ctor of stack object D (auStack_158)
    // TODO(phase2): 0x000ea20c D.method()
    // TODO(phase2): 0x000e09ec D.method()
    // TODO(phase2): 0x000e0548 ctor of stack object E (auStack_cc)
    // TODO(phase2): 0x000e0694 E.method() (thunk)
    // TODO(phase2): 0x000e05f0 E.method()
    // TODO(phase2): 0x000ea694 ctor of stack object F (auStack_40)
    // TODO(phase2): 0x000eb6c0 F.method()
    // TODO(phase2): 0x00060ac8 F.start() (thunk)
    // TODO(phase2): 0x000eb760 ctor of stack object G (auStack_2c)
    // TODO(phase2): 0x000eb8b4 G.method()
    // TODO(phase2): 0x00060ac8 G.start() (thunk)
    // TODO(phase2): 0x000eba70 ctor of stack object H (auStack_1a68, 6200 bytes)
    // TODO(phase2): 0x000ec2b8 H.method()
    // TODO(phase2): 0x00060ac8 H.start() (thunk)
    // TODO(phase2): 0x000aa5bc ctor of stack object I (auStack_5c)
    // TODO(phase2): 0x000aa6bc I.method()
    // TODO(phase2): 0x00060ac8 I.start() (thunk)
    // TODO(phase2): 0x000aa0d0 ctor of stack object J (auStack_80)
    // TODO(phase2): 0x000aa1bc J.method() (thunk)
    // TODO(phase2): 0x000aa12c J.method()
    // TODO(phase2): 0x00060384()
    // TODO(phase2): 0x000604b8()
    // TODO(phase2): 0x00056040(0x00055fd4 instance(), 1)
    // TODO(phase2): 0x0005a3ec(0x00055fd4 instance(), 1)
    // TODO(phase2): 0x000ad0a8 instance()
    // TODO(phase2): 0x000ad104()
    // TODO(phase2): 0x000ad0a8 instance()
    // TODO(phase2): 0x000ad1a4()
    // TODO(phase2): 0x000ad0a8 instance()
    // TODO(phase2): 0x000ae1f0()
    // TODO(phase2): 0x000d6fe0(0x000d82e0 instance(), 0)
    // TODO(phase2): 0x000afb84()
    // TODO(phase2): 0x000b70b0()
    // TODO(phase2): 0x00046b38()
    // TODO(phase2): 0x00049da8()
    // TODO(phase2): 0x0034860c()
    // TODO(phase2): 0x003476a8()

    log_simple("INFO", " ==== HMI ==== INFO: HMI main end===================================");
    printlog(3, "INFO", "main.cpp", "main", 0xee,
             " ==== HMI ==== INFO: HMI main end===================================");

    const bool dev_screen = DevDrawScreen();

    const int ret = app.exec();
    if (dev_screen) {
        IgdtTextTerm();
        IgdtGlTerm();
    }

    // TODO(phase2): stack objects are destroyed in reverse order on return:
    // J 0x000aa168, I 0x000aa600, H 0x000ebec8, G 0x000eb78c, F 0x000ea6c8,
    // E 0x000e0594, D 0x000e0940, C 0x00063664, B 0x00030680, then
    // ~QCoreApplication, then A 0x000de6a8.
    return ret;
}
