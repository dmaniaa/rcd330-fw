#include "display/igdt/extend/igdt_wrapper.h"

#include "display/igdt/igdt_gl.h"
#include "display/igdt/igdt_text.h"
#include "service/osal/osal_api.h"

namespace {

bool g_igdtShown = false;   // 0x00973fc1
bool g_igdtGlReady = false;

} // namespace

// 0x00060e3c hmi_wrapper_igdt_init
void hmi_wrapper_igdt_init() {
    log_simple("INFO", " ==== HMI ==== INFO: Started..\n");
    printlog(3, "INFO", "display/igdt/extend/igdt_wrapper.cpp", "hmi_wrapper_igdt_init", 0x8b,
             " ==== HMI ==== INFO: Started..\n");
    // TODO(phase2): 0x00320c7c IGDT cold init (mutex, tables, 0x0031e01c)
    log_simple("INFO", " ==== HMI ==== INFO: coldinit done..\n");
    printlog(3, "INFO", "display/igdt/extend/igdt_wrapper.cpp", "hmi_wrapper_igdt_init", 0x8e,
             " ==== HMI ==== INFO: coldinit done..\n");
    // TODO(phase2): 0x003216fc IGDT interface start
    log_simple("INFO", " ==== HMI ==== INFO: if_start done..\n");
    printlog(3, "INFO", "display/igdt/extend/igdt_wrapper.cpp", "hmi_wrapper_igdt_init", 0x91,
             " ==== HMI ==== INFO: if_start done..\n");
    // TODO(phase2): 0x0031bc38(1), 0x0031d210(1), memset(0x009705a8, 0xff, 0x38fc),
    // memset(0x00973ec0, 0, 0xff), DAT_00973fbf = 0xff
}

// 0x00060f54 hmi_wrapper_igdt_show
void hmi_wrapper_igdt_show() {
    // TODO(phase2): DAT_00973fc0 = 1 (show in progress)
    if (!g_igdtShown) {
        g_igdtShown = true;
        // 0x0031d60c: draw-state reset, then 0x00326084.
        // TODO(phase2): the rest of 0x0031d60c (DAT_00a0fa38 = 0, DAT_00a0fa34 = 0x00321f5c()).
        g_igdtGlReady = IgdtGlInit(800, 480);
        if (!g_igdtGlReady) {
            // DEVIATION: stock ignores 0x00326084's result.
            log_simple("INFO", " ==== HMI ==== INFO: IGDT GL init failed\n");
        }
        // DEVIATION: stock starts FreeType lazily on the first text draw
        // (0x0031d2a4); font paths are relative to /opt like the textures.
        IgdtTextInit("/opt");
        log_simple("INFO", " ==== HMI ==== INFO: igdt_show \n");
        printlog(3, "INFO", "display/igdt/extend/igdt_wrapper.cpp", "hmi_wrapper_igdt_show", 0xdd,
                 " ==== HMI ==== INFO: igdt_show \n");
        // TODO(phase2): 0x000517f4(0x00052174 instance(), 1)
    }
    // TODO(phase2): 0x003216c4 IGDT update, and when DAT_00973ea4 is set:
    // 0x00320d48, 0x0032177c and the VPowerProxy follow-ups.
    // TODO(phase2): DAT_00973fc0 = 0
}

bool hmi_wrapper_igdt_gl_ready() {
    return g_igdtGlReady;
}
