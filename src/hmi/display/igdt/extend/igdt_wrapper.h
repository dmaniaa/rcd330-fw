// Stock path display/igdt/extend/igdt_wrapper.cpp (printlog file names).
//
// hmi_wrapper_igdt_* : the HMI's entry points into IGDT.
#ifndef RCD330_HMI_DISPLAY_IGDT_EXTEND_IGDT_WRAPPER_H
#define RCD330_HMI_DISPLAY_IGDT_EXTEND_IGDT_WRAPPER_H

// 0x00060e3c: IGDT cold init and interface start (logs only so far).
void hmi_wrapper_igdt_init();

// 0x00060f54: the first call brings up EGL/GLES2 (0x0031d60c -> 0x00326084);
// every call then runs one IGDT update (phase 2).
void hmi_wrapper_igdt_show();

// True once hmi_wrapper_igdt_show brought up EGL/GLES2 successfully.
// DEVIATION: stock has no such query (it ignores 0x00326084's result).
bool hmi_wrapper_igdt_gl_ready();

#endif
