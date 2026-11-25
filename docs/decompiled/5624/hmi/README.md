[← Decompiled Firmware Index](../README.md)

# `opt/hmi` — Main Infotainment HMI Application (`5624_vw`)

- **Target binary**: `opt/hmi` (`9,832,364` bytes, ARMv7-A Linux ELF, SHA-256 `44ccad4211fc8f2fb21f2db46a04271155b8d894735427802baee7467630a6f2`)
- **Decompiled**: `9,580` written / `10,065` total / `484` external / `1` failed (`_INIT_3` @ `0x11658`) in [functions/](functions)
- **VA → File Offset**: `PT_LOAD[0]` (`0x8000..0x95efa0`) `file_off = va - 0x8000`; `PT_LOAD[1]` (`0x967000..`) `file_off = va - 0x10000`
- **Deep specs**: [hmi-internals.md](../../../hmi-internals.md) · [assets-and-localization.md](../../../assets-and-localization.md) · [Android Auto ECNR Echo Bug](../../../bugs/android-auto-echo-ecnr.md)

## Overview

Primary 800×480 infotainment UI and state-machine application spawned by `opt/pwr_agent`. Links `libQtCore.so.4` (no `libQtGui`) solely for its event loop, signals/slots, timers, and threads; all rendering is performed via Visteon's proprietary **IGDT** OpenGL ES 2.0 engine (`0x31b000..0x32a500`) over Vivante EGL/GLES2 + FreeType + CxImage (`libximage.so.1`). Communicates with every background service over `opt/vmf` via 18 `VBaseProxy` subsystem proxies.

## Companion Reference Artifacts in This Folder

- [decompile-index.json](decompile-index.json) & [decompile-journal.jsonl](decompile-journal.jsonl) — Complete `rea` `decompile_all` manifest and journal.
- [hmi_symbols_5624.json](hmi_symbols_5624.json) — ELF metadata, dynamic symbols, key `.rodata` table addresses, and 240+ recovered original `.cpp`/`.c` source file mappings (`1,219` logged functions).
- [diff-5624-vs-5606.md](../../../diff-5624-vs-5606.md) — Full behavioral, ELF, VIP, asset, INI, and string-table comparison between `5624_vw` and `5606_skoda` (with differing binary exports in [diff-5624vs5606/](../../diff-5624vs5606/README.md)).
- [gresfiles_catalog_5624.json](gresfiles_catalog_5624.json) & [gresfiles_catalog_5606.json](gresfiles_catalog_5606.json) — Decoded `_ovg.bin` texture descriptor catalogs (`32,110` slots / `2,806` unique textures in `5624`).

## Key Subsystems & Functions

### Startup, Control Loop, Input & Page Management
- [`0x23468`](functions/0x23468.c) — `entry`
- [`0x10e58`](functions/0x10e58.c) — `main` (calls `logo_black_avoid_handle` @ [`0x23514`](functions/0x23514.c), reads `/pss1/hmi_autotest.ini` & `/pss1/stdplus.ini`, starts Qt 4.7.4 event loop & HMI threads)
- [`0x238d4`](functions/0x238d4.c) — `VMultiTouch::Init` (`ts_open` / `ts_config` touchscreen input on `/dev/event1`)
- [`0xaa6d4`](functions/0xaa6d4.c), [`0xaa77c`](functions/0xaa77c.c) — `VKeyDriver` (`/dev/event0` physical faceplate buttons `EV_KEY 1..10`)
- [`0xaa250`](functions/0xaa250.c), [`0xaa3e0`](functions/0xaa3e0.c) — `VKnobDriver` (`/dev/tuner_knob` rotary encoder `0x32`/`0x33`)
- [`0x30064`](functions/0x30064.c) — `VControlTask::Run`
- [`0x2c34c`](functions/0x2c34c.c) — `VPageManager::ShowPage` (2,618-line page transition state machine)
- [`0x636d4`](functions/0x636d4.c) — `VDisplayTask::LaunchAppByPhoneViewBtn`

### VMF IPC Core & Subsystem Proxies (`control_logic/subsystem/*`)
- [`0x5fdd4`](functions/0x5fdd4.c) — `VBaseProxy::RegisterVmfGroup`
- [`0x5fe68`](functions/0x5fe68.c) — `VBaseProxy::PackMsg` (`[0]=2`, `[0x14]=grp`, `[0x15]=sub`, `*(u16*)(+0x18)=len`, `memcpy(+0x1a, ...)`)
- [`0x5ff4c`](functions/0x5ff4c.c) — `VBaseProxy::SendVmfMsg`
- [`0x60428`](functions/0x60428.c), [`0x605f0`](functions/0x605f0.c), [`0x606e8`](functions/0x606e8.c) — `VmfThread` connection, group dispatch, and transmit
- [`0x46b80`](functions/0x46b80.c) (`VAudioProxy`), [`0x5601c`](functions/0x5601c.c) (`VPowerProxy`), [`0xad0f0`](functions/0xad0f0.c) (`VAMFMProxy`), [`0xafbd0`](functions/0xafbd0.c) (`VBAPProxy`), [`0xbf780`](functions/0xbf780.c) (`VBTProxy`), [`0xd59d0`](functions/0xd59d0.c) (`VMediaProxy`), [`0xdc520`](functions/0xdc520.c) (`VMirrorLinkProxy`), [`0x1c33d4`](functions/0x1c33d4.c) (`VPSSProxy`), [`0x1fc6a4`](functions/0x1fc6a4.c) (`VDiagProxy`), [`0x2012a4`](functions/0x2012a4.c) / [`0x2016e0`](functions/0x2016e0.c) (`VServiceProxy`), [`0x202218`](functions/0x202218.c) (`VSystemTimeCalendarProxy`), [`0x32cc70`](functions/0x32cc70.c) (`VCarPlayProxy`), [`0x346fb4`](functions/0x346fb4.c) (`VAppConnectProxy`), [`0x36b0a4`](functions/0x36b0a4.c) (`VCarLifeProxy`), [`0x382c90`](functions/0x382c90.c) (`VAndroidAutoProxy`)

### Reverse Camera (RVC), Framebuffer Alpha & HMI Pages
- [`0x5f284`](functions/0x5f284.c)..[`0x5f978`](functions/0x5f978.c) — `/dev/fb0` local/global alpha & color-key control (`MXCFB_SET_LOC_ALPHA`, `MXCFB_SET_GBL_ALPHA`, `MXCFB_SET_CLR_KEY`)
- [`0xde600`](functions/0xde600.c), [`0xdf8d4`](functions/0xdf8d4.c), [`0xac2e0`](functions/0xac2e0.c), [`0x4c084`](functions/0x4c084.c) — `VCameraTask`, `mxc_v4l_overlay_config`, `VCameraNotifyFilter::HandleCameraRvc`, `VCameraProxy::CloseCameraTVIN`
- [`0x154078`](functions/0x154078.c) — `VSystemSetupMainPage::UpdateAllText`
- [`0x1666ec`](functions/0x1666ec.c), [`0x166774`](functions/0x166774.c) — Language selection presentation handlers
- [`0x1b1288`](functions/0x1b1288.c), [`0x1fb9b8`](functions/0x1fb9b8.c), [`0x2290f8`](functions/0x2290f8.c) — `VEtmMain::OnBtnRestoreClicked` & `VETMHmiLookFeelPage` SD card `/dev/mtd5` boot logo flasher
- [`0x257a84`](functions/0x257a84.c), [`0x297308`](functions/0x297308.c) — `VCarEntryMain` & `VCarMfaPage`

### IGDT Core Engine (`0x31b000..0x32a500`)
- [`0x326084`](functions/0x326084.c) (EGL/GLES2 init, FBO & 5 shader programs), [`0x326a50`](functions/0x326a50.c) (frame begin), [`0x326b3c`](functions/0x326b3c.c) (`eglSwapBuffers` frame flush, vtable slot 8), [`0x326ee8`](functions/0x326ee8.c) (image draw), [`0x325728`](functions/0x325728.c) (`_ovg.bin` texture loader with inline PackBits RLE), [`0x3278e0`](functions/0x3278e0.c) (FreeType text/glyph draw), [`0x329ea0`](functions/0x329ea0.c) (text line measure/layout), [`0x31d2a4`](functions/0x31d2a4.c) / [`0x31d5ec`](functions/0x31d5ec.c) (FreeType 2.4.8 font loading + CJK fallback), [`0x31f8e4`](functions/0x31f8e4.c) / [`0x32072c`](functions/0x32072c.c) (declarative scene-graph walker), and [`0x323dfc`](functions/0x323dfc.c) (scene-state updater).
