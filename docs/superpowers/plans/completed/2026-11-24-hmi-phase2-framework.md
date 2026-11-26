# HMI Recreation — Phase 2 (Qt moc, IGDT Runtime, VMF, Tasks, Filters, Controls, PageManager, Interactive Start/App-Connect) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the complete stock `opt/hmi` application framework — host-built Qt 4.7.4 `moc`, dynamic multi-layer IGDT runtime with touch hit-testing, `VBaseTask`/`VmfThread`/`VBaseProxy` VMF IPC, boot/status-bar subsystem proxies, `VControlTask` + notify filter chain, input drivers (`VKeyDriver`/`VKnobDriver`/`VMultiTouch`), core widget hierarchy + `ControlCreator`, `VDisplayTask`/`VPageManager`/`VBasePage`, and live interactive start/App-Connect screens (`VTunerMainPage` boot shell `0xf9`, `vAppNoConnection` `0x04`, `vAppConnectInfo` `0x03`, `VStatusLine` `0xfe`) — removing the Phase 1 `HMI_DEV_SCREEN` hook and parity masks so the recreated HMI boots under `pwr_agent` and interacts with hardkeys, touchscreen, and tune knob identically to stock in QEMU.

**Architecture:** Structural port of stock `opt/hmi` (spec §4 approach A), strictly preserving stock source paths, class names, Qt 4 signal/slot strings, VMF 26-byte framing, 29-layer IGDT compositing, and 1:1 VA traceability. Pure-data/state logic (IGDT dynamic tables & touch hit-test, VMF packet framing/dispatch, proxy payload decoders, filter routing, input event decoding, 3-slice control layout & button state machine) is factored into portable C++98 translation units tested on the macOS host with `ctest`; the ARMv7 device binary adds the Qt 4.7.4 `moc`/thread/IPC/GL/tslib glue and is verified end-to-end under QEMU `-M rcd330` against unmodified stock `5624_vw`.

**Tech Stack:** C++98 (`-std=gnu++98`, GCC 4.6 libstdc++ headers) compiled by `zig clang` (0.17.1) for `armv7a-linux-gnueabi`, CMake + Ninja, host-built Qt 4.7.4 `moc` (output revision 62), Qt Embedded 4.7.4 `libQtCore.so.4`, `libvmf_client.so`, `libts-1.0.so.0` (Visteon 24-byte `ts_sample`), `libOSAL.so.1`, EGL/GLES2, FreeType 2.4.8, Python 3 (`unittest`), QEMU `-M rcd330` (VIP TCP + QMP `input-send-event`).

**Spec:** `docs/superpowers/specs/2026-11-24-hmi-recreation-design.md`

## Global Constraints

- Target: `armv7a-linux-gnueabi`, `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp`, glibc **2.13** symbol versions, `zig` **0.17.1** pinned in `src/hmi/cmake/zig-armv7-toolchain.cmake`.
- C++ runtime: device `libstdc++.so.6.0.16` (GCC 4.6). Max symbol versions in `build/hmi/hmi`: `GLIBCXX_3.4.16`, `CXXABI_1.3.5`, `GLIBC_2.13`. Never link zig's `libc++`.
- `DT_NEEDED` of `build/hmi/hmi` must remain a subset of stock `extracted-fw/5624_vw/opt/hmi` (Phase 2 adds only `libvmf_client.so` and `libts-1.0.so.0`, both in stock `DT_NEEDED`).
- Qt symbol safety: `build/hmi/hmi` may import only Qt symbols already imported by stock 5624 binaries (`tools/tests/test_hmi_build.py::TestHmiBuild::test_qt_imports_subset_of_stock`), with `QT_NO_DEBUG` enabled.
- Sysroot for linking: `build/cache/std_plus_pss-6.5inch_5624_VW_PQ_ROW/stage`.
- Source paths and class names follow recovered 5624 paths (`control_logic/...`, `display/...`, `service/...`). Files whose stock name is unknown begin with `// File name is ours; stock name unknown.`
- Origin tags on the line immediately above every recreated function: `// 0x<8-hex VA> <Name>` (lowercase 8-hex 5624 VA verified in `docs/decompiled/hmi/hmi_symbols_5624.json`). Deviations carry `// DEVIATION: <why>`; reproduced stock bugs carry `// STOCK-QUIRK: <what>`; Phase 3 deferred bodies carry `// TODO(phase3): <what>`.
- Regenerate `src/hmi/TRACE.json` via `python3 src/hmi/tools/gen_trace.py` whenever origin tags are added or moved (`test_hmi_build.py::TestHmiTrace`).
- Never commit `extracted-fw/`, `original-fw/`, or `build/`. Generated tables and `moc_*.cpp` stay under `build/hmi/`.
- Every file ends with a trailing newline. Commit after each task (`--no-verify` forbidden).

## Review Focus

1. **Qt 4.7.4 `moc` symbol drift against device `libQtCore.so.4`** — sysroot carries a Qt 4.8.2 stand-in while real units run Qt Embedded 4.7.4; host-built `moc` must emit revision 62 code using only Qt symbols stock 5624 binaries import (`QMetaObject::activate`, `QObject::qt_metacall`, `QObject::qt_metacast`, no `QMetaObjectExtraData`-only 4.8 APIs or `qFlagLocation`). Pinned by `test_moc_revision_62` and `test_qt_imports_subset_of_stock` in Task 1 (re-run by every subsequent task's `tools/build_hmi.sh`).
2. **Malformed VMF packets and daemon-not-ready startup races** — `VBaseProxy::PackMsg` (`0x5fe68`) receiving NULL, `< 4` bytes, big-endian length `!= len - 4`, or payload `> 0x4b4` must reject cleanly without overflowing the `0x4d0` packet buffer or transmitting garbage to `vgw`/VIP/PSS; `VmfThread::Connect` (`0x60428`) must retry `nw_vmf_connect` up to 10 times with 2 ms backoff and lock listener list traversal with `QMutex`. Pinned by `test_vmf_core.cpp` in Task 4.
3. **24-byte Visteon `struct ts_sample`, out-of-range input codes, and drag-out touch cancel** — device `libts-1.0.so.0` writes 24-byte `ts_sample` records (`0x23cac`, not upstream 20-byte); `/dev/event0` may deliver non-`EV_KEY` or `code > 10`; `/dev/tuner_knob` may yield partial reads (`!= 4`) or unknown words; dragging a finger off a pressed button before lifting must tag the release with `0x80000000` so `IgdtButtonBase::TouchEventHandler` (`0x2ea27c`) does NOT emit `SigClicked`. Pinned by `test_igdt_touch.cpp` (Task 3) and `test_input_drivers.cpp` (Task 7).
4. **Out-of-range IGDT tags/indices and mid-touch page transitions** — mutating a static tag (bit 30 unset), unknown node kind, out-of-bounds dynamic index (`dyn_grp >= 0x30ba`, `dyn_img >= 0x6cfd`, `dyn_txt >= 0xe2a`, `branch >= 0x10f0`, `state >= 0x1b11`), or `screen_id >= 255` must return stock error codes (`0x1004`/`0x1008`/`0x10ff`/`0x80`) with zero out-of-bounds RAM writes; inserting/removing a screen while a touch is captured (`0x610a8`) must invalidate the captured bounding box (`0x40000000` cancel) so no stale button click fires. Pinned by `test_igdt_runtime.cpp` (Task 2) and `test_igdt_touch.cpp` (Task 3).
5. **Unimplemented Phase-3 hardkeys and VMF page requests** — pressing `MEDIA`, `PHONE`, `SETUP`, or receiving VMF traffic targeting Phase-3 pages/proxies must log once, never crash, never corrupt `b_hardkey_permit`/`b_touch_permit`, and leave the active screen responsive to subsequent `APP`/`RADIO`/touch input with zero `pwr_agent` restarts. Pinned by `test_filters.cpp` (Task 6) and `test_hmi_interactive.py` (Task 10).

---

## File Structure

```
tools/build_hmi.sh                                  # + extract/build host Qt 4.7.4 moc -> build/hmi/host-tools/moc
tools/run_rcd330.sh                                 # + --qmp <unix-socket> for headless touchscreen injection
tools/hmi_qemu.py                                   # + touch(x, y) via QMP input-send-event, knob(steps) via VIP ENC TUNE
tools/tests/test_hmi_build.py                       # + test_moc_revision_62, test_dt_needed includes vmf/tslib subset
tools/tests/test_hmi_tables.py                      # + checks for live node graph, touch paths, dyn record counts
tools/tests/test_hmi_parity.py                      # remove HMI_DEV_SCREEN & Phase-1 dynamic/glow masks; add AppConnectInfo parity
tools/tests/test_hmi_interactive.py                 # NEW: QEMU boot -> APP -> touch Info -> touch Back -> RADIO -> knob -> Phase-3 key safety

src/hmi/CMakeLists.txt                              # + hmi_wrap_cpp(), rcd330_vmf_client, rcd330_ts, all Phase 2 sources
src/hmi/tools/moc/CMakeLists.txt                    # NEW: standalone host CMake build of Qt 4.7.4 moc + POSIX bootstrap
src/hmi/tools/moc/host_qconfig.h                    # NEW: minimal host Qt 4.7.4 config for moc bootstrap compilation
src/hmi/tools/gen_igdt_tables.py                    # emit full 76,513-node tree, 2,496 touch paths, static/dyn init records, branch/state/clip/flags

src/hmi/display/igdt/igdt_tables.h                  # live scene-graph & init-table C structs mirroring stock .rodata
src/hmi/display/igdt/igdt_runtime.{h,cpp}           # NEW (portable): RAM copies of dyn_grp/img/txt, branch/state, 29 layers, setters/getters
src/hmi/display/igdt/igdt_touch.{h,cpp}             # NEW (portable): 60-slot touch queue (0x324aec) + reverse-layer hit-test (0x324b90)
src/hmi/display/igdt/igdt_gl.{h,cpp}                # + IgdtDrawRect (0x32749c), full 3x3 matrix + pivot, dynamic tint
src/hmi/display/igdt/igdt_scene.{h,cpp}             # live multi-layer node-tree walk (0x32072c, 0x31f8e4, 0x3202c4, 0x31f0a0, 0x31f41c)
src/hmi/display/igdt/extend/igdt_wrapper.{h,cpp}    # full stock hmi_wrapper_igdt_* API + VReflash Q_OBJECT render thread (0x62e60..0x632bc)

src/hmi/service/vmf/vmf_api.h                       # NEW: extern "C" libvmf_client.so prototypes
src/hmi/service/vmf/vmf_frame.{h,cpp}               # NEW (portable): 26-byte VmfPacket PackMsg (0x5fe68) + group listener fan-out
src/hmi/service/vmf/VmfThread.{h,cpp}               # NEW: VmfThread QThread singleton (0x602e0..0x60720)
src/hmi/service/utility/vbasetask.{h,cpp}           # NEW: VBaseTask QObject+QThread base (0x609f8..0x60ae8)
src/hmi/service/utility/vbaseproxy.{h,cpp}          # NEW: VBaseProxy (0x5fdb4..0x5ff4c)
src/hmi/service/tslib/tslib_api.h                   # NEW: 24-byte Visteon struct ts_sample + ts_open/config/read/close

src/hmi/control_logic/subsystem/
  power/vpowerproxy.{h,cpp}                         # NEW: VPowerProxy (0x55fd4..0x5bab4)
  display/vdisplayproxy.{h,cpp}                     # NEW: VDisplayProxy (0x52174..0x54fcc)
  appconnect/vappconnectproxy.{h,cpp}               # NEW: VAppConnectProxy (0x346fb4..0x34ac5c)
  audio/vaudioproxy.{h,cpp}                         # NEW: VAudioProxy boot/status-bar subset (0x46b38..0x49da8)
  bap/vbapproxy.{h,cpp}                             # NEW: VBAPProxy boot/temp/clamp/headline subset (0xafb84..0xb70b0)
  amfm/vamfmproxy.{h,cpp}                           # NEW: VAMFMProxy boot queries (0xad0a8..0xae1f0)
  media/vmediaproxy.{h,cpp}                         # NEW: VMediaProxy boot queries (0xd59d0..0xd82e0)
  system_time_calendar/vsystemtimecalendarproxy.{h,cpp} # NEW: RTC time proxy (0x2021d0..0x202218)
  pss/vpssproxy.{h,cpp}                             # NEW: VPSSProxy (0x1c338c..0x1c5a90)
  diag/vdiagproxy.{h,cpp}                           # NEW: VDiagProxy status/lock queries (0x1fc6a4..0x1ffc10)
  phase3_stubs.{h,cpp}                              # NEW: minimal VMF group registration shells for BT/ML/CP/CL/AA/Service/Subsystem (Phase 3)

src/hmi/control_logic/mode/
  vbasefilter.{h,cpp}                               # NEW (portable): FILTER_MSG + VBaseFilter chain (0x39780)
  power/vpowernotifyfilter.{h,cpp}                  # NEW: VPowerNotifyFilter (0x45ea4..0x46054)
  display/vdisplaycmdfilter.{h,cpp}                 # NEW: VDisplayCmdFilter (0x40e18..0x40e38)
  ipcl/vipclnotifyfilter.{h,cpp}                    # NEW: VIPCLNotifyFilter (0x41470..0x43c1c)
  audio/vaudionotifyfilter.{h,cpp}                  # NEW: VAudioNotifyFilter chain node
  media/vmedianotifyfilter.{h,cpp}                  # NEW: VMediaNotifyFilter chain node
  camera/vcameranotifyfilter.{h,cpp}                # NEW: VCameraNotifyFilter chain node
src/hmi/control_logic/vcontroltask.{h,cpp}          # NEW: VControlTask (0x2fd18..0x36270)
src/hmi/control_logic/input/
  vkeydriver.{h,cpp}                                # NEW: VKeyDriver (/dev/event0, 0xaa5bc..0xaa77c) + portable decoder
  vknobdriver.{h,cpp}                               # NEW: VKnobDriver (/dev/tuner_knob, 0xaa0d0..0xaa3e0) + portable decoder

src/hmi/display/multitouch/vmultitouch.{h,cpp}      # NEW: VMultiTouch (tslib, 0x238d4..0x23dbc) + portable state machine
src/hmi/display/controls/controlbase/
  vvisibilitybase.{h,cpp}, vmoveablebase.{h,cpp}, vdynamicbase.{h,cpp},
  itembase.{h,cpp}, complexcontrol.{h,cpp}, buttonbase.{h,cpp}, igdtbuttonbase.{h,cpp}
src/hmi/display/controls/controlparts/
  igdttextitem.{h,cpp}, compositebackground.{h,cpp}, compositeitem.{h,cpp},
  igdtbutton.{h,cpp}, vstatusline.{h,cpp}
src/hmi/display/base/controlcreator.{h,cpp}         # NEW: ControlCreator (0x2d6884..0x2dd4e0)
src/hmi/display/page/base/
  basepage.{h,cpp}                                  # NEW: VBasePage (0x2a7d0..0x2adf0)
  pagemanager.{h,cpp}                               # NEW: VPageManager (0x2b2a4..0x2c34c)
src/hmi/display/page/appconnect/
  vappnoconnection.{h,cpp}                          # NEW: vAppNoConnection eScrId 0x04 (0x33a660..0x33ae70)
  vappconnectinfo.{h,cpp}                           # NEW: vAppConnectInfo eScrId 0x03 (0x345a60..0x3462c8)
src/hmi/display/page/tuner/vtunermainpage.{h,cpp}   # NEW: VTunerMainPage eScrId 0xf9 Phase-2 boot shell (Phase 3 fills tuner logic)
src/hmi/display/vdisplaytask.{h,cpp}                # NEW: VDisplayTask (0x63344..0x657dc)
src/hmi/main.cpp                                    # logo_black_avoid_handle (0x23514) + full startup sequence (0x10e58), drop HMI_DEV_SCREEN

src/hmi/tests/
  CMakeLists.txt                                    # register all new portable C++98 host unit tests
  test_igdt_runtime.cpp, test_igdt_touch.cpp, test_vmf_core.cpp,
  test_proxies.cpp, test_filters.cpp, test_input_drivers.cpp, test_controls.cpp
src/hmi/TRACE.json, src/hmi/README.md, docs/hmi-internals.md
```

---

### Task 1: Host-Built Qt 4.7.4 `moc` and CMake `hmi_wrap_cpp` + `libvmf_client`/`libts` Targets

**Files:**
- Create: `src/hmi/tools/moc/CMakeLists.txt`, `src/hmi/tools/moc/host_qconfig.h`, `src/hmi/service/utility/vbasetask.h`, `src/hmi/service/utility/vbasetask.cpp`
- Modify: `tools/build_hmi.sh`, `src/hmi/CMakeLists.txt`, `tools/tests/test_hmi_build.py`

**Interfaces:**
- Consumes: Cached `build/hmi/third_party/downloads/qt-everywhere-opensource-src-4.7.4.tar.gz` (from Phase 0-1 `tools/build_hmi.sh`), staged sysroot `libvmf_client.so` and `libts-1.0.so.0`.
- Produces:
  - Host binary `build/hmi/host-tools/moc` (Qt 4.7.4 `moc`, `mocOutputRevision == 62`).
  - CMake function `hmi_wrap_cpp(out_var header1 ...)` in `src/hmi/CMakeLists.txt` passing `-DQT_NO_DEBUG -I<qt_config> -I<qt474>/include -I<src/hmi>` to `RCD330_MOC_EXECUTABLE` and appending generated `moc_<stem>.cpp` files to `${out_var}`.
  - CMake IMPORTED libraries `rcd330_vmf_client` (`${RCD330_SYSROOT}/opt/lib/libvmf_client.so`) and `rcd330_ts` (`${RCD330_SYSROOT}/usr/lib/libts-1.0.so.0`).
  - `class VBaseTask : public QObject` (`Q_OBJECT`, `0x609f8` ctor, `0x60a50` dtor, `0x60ac8` `Start`, `0x60ae8` `LogThread`, `SigInitialize()`, `InitializeSlot()`).

- [ ] **Step 1: Write failing test in `tools/tests/test_hmi_build.py`**

Add a test class `TestHmiMoc` to `tools/tests/test_hmi_build.py` verifying that `build/hmi/host-tools/moc` exists, prints Qt 4.7.4 version, and generates revision-62 output without 4.8-only symbols (`qt_static_metacall` / `QMetaObjectExtraData`):

```python
MOC = ROOT / "build/hmi/host-tools/moc"


class TestHmiMoc(unittest.TestCase):
    def setUp(self):
        if not MOC.is_file():
            if os.environ.get("RCD330_REQUIRE_HMI") == "1":
                self.fail(f"{MOC} missing; run tools/build_hmi.sh")
            self.skipTest(f"{MOC} not built")

    def test_moc_revision_62(self):
        ver = subprocess.run([str(MOC), "-v"], capture_output=True, text=True)
        combined = ver.stdout + ver.stderr
        self.assertEqual(ver.returncode, 0, combined)
        self.assertIn("4.7.4", combined)
        sample = (
            "#include <QtCore/QObject>\n"
            "class ProbeObj : public QObject {\n"
            "    Q_OBJECT\n"
            "signals:\n"
            "    void ping(int v);\n"
            "public slots:\n"
            "    void pong(int v);\n"
            "};\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            hdr = pathlib.Path(tmp) / "probe.h"
            hdr.write_text(sample)
            out = subprocess.check_output(
                [str(MOC), "-DQT_NO_DEBUG", str(hdr)], text=True
            )
        self.assertIn("cannot be used with the include files", out)
        self.assertIn("62", out)
        self.assertIn("QMetaObject::activate", out)
        self.assertNotIn("qt_static_metacall", out)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v`
Expected: FAIL in `TestHmiMoc::test_moc_revision_62` with `build/hmi/host-tools/moc missing`.

- [ ] **Step 3: Create `src/hmi/tools/moc/host_qconfig.h` and `src/hmi/tools/moc/CMakeLists.txt`**

Create `src/hmi/tools/moc/host_qconfig.h`:

```cpp
// File name is ours; stock name unknown.
// Minimal host configuration for compiling Qt 4.7.4 src/tools/moc on macOS/Linux.
#ifndef RCD330_HOST_QCONFIG_H
#define RCD330_HOST_QCONFIG_H

#define QT_PRODUCT_LICENSEE "Open Source"
#define QT_PRODUCT_LICENSE "OpenSource"
#ifndef QT_EDITION
#  define QT_EDITION QT_EDITION_OPENSOURCE
#endif
#define Q_BYTE_ORDER Q_LITTLE_ENDIAN
#define QT_POINTER_SIZE 8
#define QT_LARGEFILE_SUPPORT 64
#define QT_NO_QOBJECT
#define QT_NO_THREAD
#define QT_NO_UNICODETABLES
#define QT_NO_CODECS
#define QT_NO_TEXTCODEC
#define QT_NO_SYSTEMLOCALE
#define QT_NO_LIBRARY

#endif
```

Create `src/hmi/tools/moc/CMakeLists.txt` compiling Qt 4.7.4 `src/tools/moc/{main,moc,preprocessor,generator,parser,token}.cpp` plus the POSIX subset of `src/tools/bootstrap/bootstrap.pro` (`qglobal.cpp`, `qmalloc.cpp`, `qnumeric.cpp`, `qabstractfileengine.cpp`, `qbuffer.cpp`, `qdatastream.cpp`, `qdir.cpp`, `qdiriterator.cpp`, `qfile.cpp`, `qfileinfo.cpp`, `qfsfileengine.cpp`, `qfsfileengine_iterator.cpp`, `qfsfileengine_unix.cpp`, `qfsfileengine_iterator_unix.cpp`, `qiodevice.cpp`, `qtemporaryfile.cpp`, `qtextstream.cpp`, `qurl.cpp`, `qmetatype.cpp`, `qvariant.cpp`, `qbitarray.cpp`, `qbytearray.cpp`, `qbytearraymatcher.cpp`, `qdatetime.cpp`, `qhash.cpp`, `qlist.cpp`, `qlocale.cpp`, `qmap.cpp`, `qregexp.cpp`, `qstring.cpp`, `qstringlist.cpp`, `qvector.cpp`, `qvsnprintf.cpp`, `qtextcodec.cpp`, `qlatincodec.cpp`, `qutfcodec.cpp`, `qisciicodec.cpp`, `qsimplecodec.cpp`, `qtsciicodec.cpp`). Do NOT compile `qcore_mac.cpp` or link `-framework CoreServices`: define `QT_BOOTSTRAPPED`, `QT_MOC`, `QT_LITE_UNICODE`, `QT_NO_CAST_FROM_ASCII`, `QT_NO_CAST_TO_ASCII`, `QT_NO_CODECS`, `QT_NO_DATASTREAM`, `QT_NO_GEOM_VARIANT`, `QT_NO_LIBRARY`, `QT_NO_QOBJECT`, `QT_NO_STL`, `QT_NO_SYSTEMLOCALE`, `QT_NO_TEXTSTREAM`, `QT_NO_THREAD`, `QT_NO_UNICODETABLES`, `QT_NO_USING_NAMESPACE`, and pass `-std=gnu++98 -Wno-deprecated-declarations -Wno-register -Wno-c++11-narrowing` (suppressing modern Clang 21 warnings on 2011 Qt4 code). Place `host_qconfig.h` as `${CMAKE_BINARY_DIR}/qt_host_config/QtCore/qconfig.h` ahead of `${RCD330_QT474_SRC_DIR}/include`, and link host `z` (`-lz`). Note: `src/tools/moc/main.cpp` includes `../../corelib/global/qconfig.cpp`; generate an empty stub `qconfig.cpp` inside `${RCD330_QT474_SRC_DIR}/src/corelib/global/qconfig.cpp` (defining `qt_configure_prefix_path_str` / `QT_LICENSEE` if referenced — check `moc/main.cpp` lines 40-70 on extraction and only provide the symbols `main.cpp` actually uses).

- [ ] **Step 4: Wire host `moc` extraction & build in `tools/build_hmi.sh` and CMake `hmi_wrap_cpp` in `src/hmi/CMakeLists.txt`**

1. In `tools/build_hmi.sh`:
   - Add `build_host_moc()` extracting `src/tools/moc`, `src/corelib`, `include/QtCore` from the already-verified `QT474_URL` tarball into `build/hmi/third_party/qt474_moc_src` (stamped with `$QT474_SHA256`), then running host `cmake -S "$SRC_DIR/tools/moc" -B "$OUT_DIR/host-tools/build"` and `cmake --build` to produce `build/hmi/host-tools/moc`. Skip rebuild if `$OUT_DIR/host-tools/moc` is executable and newer than `src/hmi/tools/moc/CMakeLists.txt` (unless `--force`).
   - Pass `-DRCD330_MOC_EXECUTABLE="$OUT_DIR/host-tools/moc"` to the ARMv7 `cmake` configure invocation.
   - Check `$SYSROOT/opt/lib/libvmf_client.so` and `$SYSROOT/usr/lib/libts-1.0.so.0` inside `check_local_firmware()`.
2. In `src/hmi/CMakeLists.txt`:
   - Require `RCD330_MOC_EXECUTABLE`.
   - Define `hmi_wrap_cpp(out_var ...)`: for each input header `${hdr}`, generate `${CMAKE_CURRENT_BINARY_DIR}/moc/moc_${rel_flat}.cpp` via `add_custom_command` invoking `${RCD330_MOC_EXECUTABLE}` with `-DQT_NO_DEBUG`, `-I${RCD330_QT_CONFIG_DIR}`, `-I${RCD330_QT474_DIR}/include`, `-I${CMAKE_CURRENT_SOURCE_DIR}`, and append to `${out_var}`.
   - Add IMPORTED targets `rcd330_vmf_client` (`${RCD330_SYSROOT}/opt/lib/libvmf_client.so`) and `rcd330_ts` (`${RCD330_SYSROOT}/usr/lib/libts-1.0.so.0`).
   - Wrap `service/utility/vbasetask.h` with `hmi_wrap_cpp(HMI_MOC_SOURCES service/utility/vbasetask.h)` and compile `service/utility/vbasetask.cpp` + `${HMI_MOC_SOURCES}` into `hmi`, exercising `moc` code generation and `test_qt_imports_subset_of_stock` end-to-end on the ARM binary (Review Focus 1).

Create `src/hmi/service/utility/vbasetask.h`:
```cpp
// File name is ours; stock name unknown.
// Stock base class for QThread-backed HMI tasks (0x000609f8..0x00060ae8).
#ifndef RCD330_HMI_SERVICE_UTILITY_VBASETASK_H
#define RCD330_HMI_SERVICE_UTILITY_VBASETASK_H

#include <QtCore/QObject>
#include <QtCore/QThread>

class VBaseTask : public QObject {
    Q_OBJECT
public:
    VBaseTask();
    virtual ~VBaseTask();

    void Start();       // 0x00060ac8
    void LogThread();   // 0x00060ae8
    QThread* Thread() { return &m_thread; }

signals:
    void SigInitialize();

protected slots:
    virtual void InitializeSlot();

protected:
    QThread m_thread;
};

#endif
```

`src/hmi/service/utility/vbasetask.cpp`:
```cpp
// File name is ours; stock name unknown.
#include "service/utility/vbasetask.h"

// 0x000609f8 VBaseTask::VBaseTask
VBaseTask::VBaseTask() : QObject(0), m_thread(0) {
    moveToThread(&m_thread);
}

// 0x00060a50 VBaseTask::~VBaseTask
VBaseTask::~VBaseTask() {
    // Stock calls only QThread::exit(0); device stock binaries do not import QThread::wait().
    m_thread.exit(0);
}

// 0x00060ac8 VBaseTask::Start
void VBaseTask::Start() {
    m_thread.start(QThread::InheritPriority);
}

// 0x00060ae8 VBaseTask::LogThread
void VBaseTask::LogThread() {
    (void)m_thread.stackSize();
    (void)m_thread.priority();
}

void VBaseTask::InitializeSlot() {}
```

- [ ] **Step 5: Run build, ABI gate, and trace check**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: `build/hmi/host-tools/moc` builds cleanly; `moc_vbasetask.cpp` compiles and links into `build/hmi/hmi`; all `TestHmiMoc`, `TestHmiBuild` (including `test_qt_imports_subset_of_stock` and `test_symbol_versions_within_device`), and `TestHmiTrace` tests PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/build_hmi.sh src/hmi/CMakeLists.txt src/hmi/tools/moc/ \
        src/hmi/service/utility/vbasetask.h src/hmi/service/utility/vbasetask.cpp \
        tools/tests/test_hmi_build.py src/hmi/TRACE.json
git commit -m "hmi: host-built Qt 4.7.4 moc, hmi_wrap_cpp, VBaseTask, and vmf/tslib CMake targets"
```

---

### Task 2: Full IGDT Scene-Graph Tables & Dynamic Multi-Layer Runtime

**Files:**
- Modify: `src/hmi/tools/gen_igdt_tables.py`, `src/hmi/display/igdt/igdt_tables.h`, `src/hmi/display/igdt/igdt_gl.h`, `src/hmi/display/igdt/igdt_gl.cpp`, `src/hmi/display/igdt/igdt_scene.h`, `src/hmi/display/igdt/igdt_scene.cpp`, `src/hmi/tests/test_igdt_textbuf.cpp`, `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`, `tools/tests/test_hmi_tables.py`
- Create: `src/hmi/display/igdt/igdt_runtime.h`, `src/hmi/display/igdt/igdt_runtime.cpp`, `src/hmi/tests/test_igdt_runtime.cpp`

**Interfaces:**
- Consumes: `hmi_elf.Elf`, `IgdtTexEntry`, `IgdtTextLayout`, `IgdtDrawState`, `IgdtDrawImage`, `IgdtDrawText`, `IgdtRuntimeText`, `IgdtGetEngString`.
- Produces:
  - Updated `src/hmi/display/igdt/igdt_tables.h` defining the live scene-graph structs mirroring stock `.rodata` (`5624_vw`):
    - `IgdtNode { uint16_t eidx; uint16_t child_count; uint32_t tag; int32_t child_first; };` (`76,513` nodes)
    - `IgdtGrpRecord { uint8_t alpha; uint16_t key; int16_t x, y; float matrix[9]; float pivot_x, pivot_y; };` (static `0x875c14`, dyn init `0x47f97c` × `0x30ba`)
    - `IgdtImgRecord { uint8_t alpha; int16_t x, y; uint32_t tint; float matrix[9]; float pivot_x, pivot_y; };` (static `0x896d38`, dyn init `0x51df44` × `0x6cfd`)
    - `IgdtTxtRecord { uint8_t alpha; int16_t x, y; uint32_t fg_color; uint32_t bg_color; float matrix[9]; float pivot_x, pivot_y; };` (static `0x874b70`, dyn init `0x759eec` × `0xe2a`)
    - `IgdtRectRecord { uint8_t alpha; int16_t x, y; uint16_t w, h; uint32_t color; };` (static `0x896cd0` + `0x8a4be8`)
    - `IgdtGrpFlags { uint8_t flags; uint8_t spacing; uint16_t clip_idx; };` (`0x454694`)
    - `IgdtTouchPath { uint32_t step_first; uint16_t step_count; };` + `const uint8_t* touch_steps;` (`2,496` paths from `scr_table +4`)
    - `IgdtTextLayout { uint32_t off; uint8_t max_chars; uint16_t width; uint8_t font; uint8_t align; uint8_t flags; };` (`0x46d6a0` × `0xe3f`, extending existing `IgdtTextLayout` in place and updating aggregate initializers in `src/hmi/tests/test_igdt_textbuf.cpp` to avoid `-Wmissing-field-initializers` under `-Werror`)
    - `IgdtScreen` extended with `uint32_t root_first; uint16_t root_count; uint32_t touch_first; uint16_t touch_count; uint32_t bg_color;`
    - `IgdtSkinTables` holding all static/init arrays, `branch_init[0x10f0]`, `state_init[0x1b11]`, `clips`, `grp_flags`, `nodes`, `touch_paths`, `touch_steps`, `text_layouts`, `textures`, `screens`.
  - `src/hmi/display/igdt/igdt_runtime.h` (portable, host-testable):
    - `enum { IGDT_LAYER_COUNT = 29, IGDT_SCREEN_NONE = 0xff, IGDT_ERR_BAD_TAG = 0x1004, IGDT_ERR_BAD_MATRIX_TAG = 0x1008, IGDT_ERR_RANGE = 0x10ff, IGDT_ERR_SCREEN = 0x80 };`
    - `struct IgdtDynGrpState { uint8_t alpha; int16_t x, y; uint32_t tint; IgdtClipRect touch_clip; float matrix[9]; };`
    - `struct IgdtDynImgState { uint8_t alpha; int16_t x, y; uint32_t tint; uint32_t fg; float matrix[9]; };`
    - `struct IgdtDynTxtState { uint8_t alpha; uint8_t align_override; bool has_align_override; int16_t x, y; uint32_t fg_color; uint32_t bg_color; float matrix[9]; };`
    - `struct IgdtRuntime { ... };` + `IgdtRuntime& IgdtGetRuntime();`
    - `void IgdtRuntimeInit(IgdtRuntime& rt, const IgdtSkinTables& t);` (`// 0x00324328`)
    - `int IgdtSetBranch(IgdtRuntime& rt, uint32_t tag, uint16_t child);` (`// 0x00322860`)
    - `int IgdtSetState(IgdtRuntime& rt, uint32_t tag, uint16_t child);` (`// 0x00322900`)
    - `int IgdtSetColor(IgdtRuntime& rt, uint32_t tag, uint32_t argb);` (`// 0x00322a50`)
    - `int IgdtSetAlpha(IgdtRuntime& rt, uint32_t tag, uint8_t alpha);` (`// 0x00322bf8`)
    - `int IgdtSetTouchArea(IgdtRuntime& rt, uint32_t tag, int16_t x, int16_t y, int16_t w, int16_t h);` (`// 0x00322e18`)
    - `int IgdtGetTouchArea(const IgdtRuntime& rt, uint32_t tag, IgdtClipRect* out);` (`// 0x00322ef4`)
    - `int IgdtSetPosX(IgdtRuntime& rt, uint32_t tag, int16_t x);` (`// 0x00323494`)
    - `int IgdtSetPosY(IgdtRuntime& rt, uint32_t tag, int16_t y);` (`// 0x003236f4`)
    - `int IgdtSetPosXY(IgdtRuntime& rt, uint32_t tag, int16_t x, int16_t y);` (`// 0x00323958`)
    - `int IgdtGetPosXY(const IgdtRuntime& rt, uint32_t tag, int16_t* x, int16_t* y);` (`// 0x003239a0`)
    - `int IgdtSetTextAlign(IgdtRuntime& rt, uint32_t tag, uint8_t h_align, uint8_t bg_mode, uint8_t trunc_mode);` (`// 0x00322690`)
    - `int IgdtSetMatrix(IgdtRuntime& rt, uint32_t tag, const float m[9]);` (`// 0x00324570`)
    - `int IgdtSetScale(IgdtRuntime& rt, uint32_t tag, float sx, float sy);` (`// 0x00324a24`)
    - `int IgdtSetRotate(IgdtRuntime& rt, uint32_t tag, float radians);` (`// 0x0032499c`)
    - `int IgdtResetNode(IgdtRuntime& rt, const IgdtSkinTables& t, uint32_t tag);` (`// 0x003240e0`)
    - `int IgdtLayerShowScreen(IgdtRuntime& rt, const IgdtSkinTables& t, uint8_t screen_id);` (`// 0x0031bd48`)
    - `int IgdtLayerHideScreen(IgdtRuntime& rt, const IgdtSkinTables& t, uint8_t screen_id);`
    - `bool IgdtConsumeDirty(IgdtRuntime& rt);`
  - `void IgdtDrawRect(uint16_t w, uint16_t h, uint32_t color);` (`// 0x0032749c`) in `igdt_gl.{h,cpp}`.
  - `bool IgdtSceneDrawLayers(const IgdtSkinTables& t, const IgdtRuntime& rt);` (`// 0x0032072c`) in `igdt_scene.{h,cpp}` (retaining `IgdtSceneDraw(t, screen_id)` as a convenience wrapper).

- [ ] **Step 1: Write failing host tests (`src/hmi/tests/test_igdt_runtime.cpp`) and Python table tests (`tools/tests/test_hmi_tables.py`)**

Create `src/hmi/tests/test_igdt_runtime.cpp`:
```cpp
#include "display/igdt/igdt_runtime.h"
#include "tests/test_runner.h"

#ifdef HMI_HAVE_IGDT_TABLES
static void test_init_and_mutations() {
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);

    // Branch 0x01000045 (VBtnInfo glow on screen 0x04) starts at child 1 in stock .rodata.
    CHECK_EQ(rt.branch[0x45], 1);
    CHECK_EQ(IgdtSetBranch(rt, 0x01000045u, 0), IGDT_OK);
    CHECK_EQ(rt.branch[0x45], 0);
    CHECK(IgdtConsumeDirty(rt));
    CHECK(!IgdtConsumeDirty(rt));

    // State 0x030000be (single App-Connect logo slot) updates cleanly.
    CHECK_EQ(IgdtSetState(rt, 0x030000beu, 2), IGDT_OK);
    CHECK_EQ(rt.state[0xbe], 2);

    // Dynamic tint color on 0x42100284 (vAppNoConnection brand line).
    CHECK_EQ(IgdtSetColor(rt, 0x42100284u, 0xff1e5affu), IGDT_OK);
    CHECK_EQ(rt.dyn_img[0x284].tint, 0xff1e5affu);

    // Dynamic text Y position on 0x45100058 (VInfoTxt on 0x04).
    CHECK_EQ(IgdtSetPosY(rt, 0x45100058u, 0x71), IGDT_OK);
    int16_t x = 0, y = 0;
    CHECK_EQ(IgdtGetPosXY(rt, 0x45100058u, &x, &y), IGDT_OK);
    CHECK_EQ(y, 0x71);

    // Dynamic group touch area (0x40100166 = VBtnInfo).
    CHECK_EQ(IgdtSetTouchArea(rt, 0x40100166u, 0, 396, 800, 84), IGDT_OK);
    IgdtClipRect area = {0, 0, 0, 0};
    CHECK_EQ(IgdtGetTouchArea(rt, 0x40100166u, &area), IGDT_OK);
    CHECK_EQ(area.y, 396);
    CHECK_EQ(area.w, 800);
    CHECK_EQ(area.h, 84);

    // Dynamic scale (3-slice stretch).
    CHECK_EQ(IgdtSetScale(rt, 0x42100284u, 2.5f, 1.0f), IGDT_OK);
    CHECK(rt.dyn_img[0x284].matrix[0] > 2.49f && rt.dyn_img[0x284].matrix[0] < 2.51f);

    // 29-layer screen show/hide: 0x04 -> layer 1, 0xfe -> layer 5.
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0x04), IGDT_OK);
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0xfe), IGDT_OK);
    CHECK_EQ(rt.layers[1], 0x04);
    CHECK_EQ(rt.layers[5], 0xfe);
    CHECK_EQ(IgdtLayerHideScreen(rt, g_igdtTablesVw, 0x04), IGDT_OK);
    CHECK_EQ(rt.layers[1], IGDT_SCREEN_NONE);
}

static void test_out_of_range_rejected() {
    // Review Focus 4: static tags, bad kinds, out-of-bounds indices, screen >= 255.
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    (void)IgdtConsumeDirty(rt);

    CHECK_EQ(IgdtSetBranch(rt, 0x010010f0u, 0), IGDT_ERR_RANGE);
    CHECK_EQ(IgdtSetState(rt, 0x03001b11u, 0), IGDT_ERR_RANGE);
    CHECK_EQ(IgdtSetColor(rt, 0x02000001u, 0xff00ff00u), IGDT_ERR_BAD_TAG);  // static (no bit 30)
    CHECK_EQ(IgdtSetColor(rt, 0x42006cfdu, 0xff00ff00u), IGDT_ERR_BAD_TAG);  // dyn_img OOB
    CHECK_EQ(IgdtSetAlpha(rt, 0x400030bau, 0x80), IGDT_ERR_BAD_TAG);         // dyn_grp OOB
    CHECK_EQ(IgdtSetPosY(rt, 0x45000e2au, 10), IGDT_ERR_BAD_TAG);            // dyn_txt OOB
    CHECK_EQ(IgdtSetScale(rt, 0x02000001u, 2.0f, 1.0f), IGDT_ERR_BAD_MATRIX_TAG);
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0xff), IGDT_ERR_SCREEN);
    CHECK(!IgdtConsumeDirty(rt));
}
#endif

int main() {
#ifdef HMI_HAVE_IGDT_TABLES
    RUN_TEST(test_init_and_mutations);
    RUN_TEST(test_out_of_range_rejected);
#endif
    return test_summary();
}
```

Add `test_scene_graph_and_touch_paths` to `tools/tests/test_hmi_tables.py` asserting that `gen_igdt_tables.load_scene_graph(elf)` extracts `76,513` unique nodes, `2,496` touch paths, screen `0x04` has `touch_count == 1`, screen `0x03` has `touch_count == 1`, screen `0xf9` has `touch_count == 74`, and `dyn_grp/dyn_img/dyn_txt/branch/state` counts match `0x30ba / 0x6cfd / 0xe2a / 0x10f0 / 0x1b11`.

Register `test_igdt_runtime` in `src/hmi/tests/CMakeLists.txt` (linking `igdt_runtime.cpp` + generated `igdt_tables_vw.cpp` when `RCD330_GEN_DIR` is present).

- [ ] **Step 2: Run tests to verify failure**

Run:
```bash
python3 -m unittest tools/tests/test_hmi_tables.py -v
./tools/build_hmi.sh --host-tests
```
Expected: FAIL (`load_scene_graph` not defined; `igdt_runtime.h` missing).

- [ ] **Step 3: Extend `gen_igdt_tables.py` & `igdt_tables.h` to emit the full scene graph and init tables**

1. Keep `SceneWalker` and `load_screens()` working identically so all existing Phase 1 tests in `test_hmi_tables.py` stay green.
2. Add `load_scene_graph(elf)` to `gen_igdt_tables.py` that:
   - Performs BFS/DFS over all 255 screens' `root` pointers (`SCR_TABLE + sid * 16`, reading `layer_group`, `root_count`, `touch_count` at `+3`, `touch_ptr` at `+4`, `bg_color` at `+8`, `root_va` at `+0xc`), emitting a contiguous `kNodes[]` table preserving contiguous child slices (`child_first`, `child_count`) exactly as stock's `cptr[0..ccnt-1]`.
   - Reads every screen's `touch_count` path pointers at `touch_ptr` (`uint32_t` array of pointers to `0xff`-terminated or depth-walked byte index sequences; inspect `0x324b90`: each step byte indexes `puVar10[1]` children until reaching the target node where `step >= child_count`, so stop storing bytes once `step >= node.child_count`, keeping that sentinel byte too, capped at 32 bytes).
   - Emits `kStaticGrps[]` (`0x875c14`), `kDynGrps[]` (`0x47f97c`, `0x30ba` entries, including `+0` alpha, `+2` group key, `+4/+6` x/y, `+8..+0x2b` 3×3 float matrix, `+0x2c/+0x30` pivot), `kStaticImgs[]` (`0x896d38`), `kDynImgs[]` (`0x51df44`, `0x6cfd` entries, `+0` alpha, `+4/+6` x/y, `+8` tint, `+0x0c..+0x2f` 3×3 matrix, `+0x30` pivot), `kStaticTxts[]` (`0x874b70`), `kDynTxts[]` (`0x759eec`, `0xe2a` entries, `+0` alpha, `+4/+6` x/y, `+8` fg, `+0xc` bg, `+0x10..+0x33` 3×3 matrix, `+0x34` pivot), `kStaticRects[]` (`0x896cd0` + `0x8a4be8`), `kGrpFlags[]` (`0x454694`), `kClipTable[]` (`0x45cca0`), `kBranchInit[0x10f0]` (`0x78f0c4`), `kStateInit[0x1b11]` (`0x7912a4`), and `kTextLayouts[0xe3f]` (`0x46d6a0`: `off`, `width`, `max_chars`, `font`, `align`, `flags`).
   - Note: `0x0047f97c` / `0x0051df44` / `0x00759eec` 3×3 matrices are 9 little-endian `float`s; verify offsets against `0x3202c4.c`, `0x31f0a0.c`, and `0x31f41c.c` (record sizes `0x34`, `0x38`, `0x3c`) before emitting.

- [ ] **Step 4: Implement `igdt_runtime.{h,cpp}`, `igdt_gl.{h,cpp}` updates, and live `igdt_scene.{h,cpp}`**

1. Implement `src/hmi/display/igdt/igdt_runtime.cpp`:
   - `IgdtRuntimeInit` (`// 0x00324328`): initializes `layers[0..28] = 0xff`, copies `branch_init` -> `branch`, `state_init` -> `state`, initializes `dyn_grp[0..0x30b9]` (including clip rect from `grp_flags[key].clip_idx` into `clip_table`, or `(0, 0, -1, -1)` when `clip_idx == 0` per `0x323c48`), `dyn_img[0..0x6cfc]`, `dyn_txt[0..0xe29]`.
   - Implement every setter/getter listed in **Interfaces** with exact stock bit checks (`tag & 0x40000000`, `(tag >> 24) & 0xf`, `tag & 0xfffff`), setting `rt.dirty = true` only when a value actually changes (matching stock dirty-bit behavior).
   - `IgdtLayerShowScreen` (`// 0x0031bd48`) places `screen_id` in `rt.layers[t.screens[screen_id].layer_group]` and sets `rt.dirty = true`; `IgdtLayerHideScreen` clears the slot to `0xff` only if it currently holds `screen_id`.
2. Update `src/hmi/display/igdt/igdt_gl.{h,cpp}`:
   - Add `IgdtDrawRect(uint16_t w, uint16_t h, uint32_t color)` (`// 0x0032749c`) using the already-compiled fill shader program (`fillColor`, `Alpha`, `MVPMatrix`).
   - Apply `state.matrix[9]` and `state.pivot_x/pivot_y` when computing the 4×4 `MVPMatrix` in `IgdtDrawImage` (`0x326ee8`) and `IgdtDrawTextCoverage` (`0x3278e0`), matching stock's column-major 2D affine transform around `(pivot_x, pivot_y)` so 3-slice stretched images (`IgdtSetScale`) render at exact pixel widths!
   - Ensure `v_skin_color` in `IgdtDrawImage` uses `state.tint` (`0xffffffff` = untinted white, otherwise ARGB tint set via `IgdtSetColor` on a dynamic group or dynamic image — `// 0x0031dad0 ApplyTint`).
3. Update `src/hmi/display/igdt/igdt_scene.{h,cpp}`:
   - Replace flattened-array loop with the live recursive walker mirroring `0x32072c` (`IgdtSceneDrawLayers`), `0x31f8e4` (`WalkNodes`), `0x3202c4` (`WalkGroup`: saves/restores `IgdtDrawState` on stack, applies static/dynamic translation, tint, alpha, clip intersection, matrix, pivot; for `KIND_BRANCH`, reads `rt.branch[node.eidx]`!), `0x31f0a0` (`DrawImageNode`), `0x31f41c` (`DrawTextNode`), `KIND_STATE` (reads `rt.state[node.eidx]`, draws img child only), and `KIND_RECT` (`IgdtDrawRect`).
   - Keep `IgdtSceneDraw(t, screen_id)` initializing a temporary runtime showing only `screen_id` so existing `main.cpp` continues compiling until Task 9 removes `HMI_DEV_SCREEN`.

- [ ] **Step 5: Run host tests, table tests, and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
python3 -m unittest tools/tests/test_hmi_tables.py tools/tests/test_hmi_build.py -v
```
Expected: All host tests (`test_igdt_strings`, `test_igdt_ovg`, `test_igdt_textbuf`, `test_igdt_runtime`), `test_hmi_tables.py`, and `test_hmi_build.py` PASS.

- [ ] **Step 6: Commit**

```bash
git add src/hmi/tools/gen_igdt_tables.py src/hmi/display/igdt/ \
        src/hmi/CMakeLists.txt src/hmi/tests/ tools/tests/test_hmi_tables.py src/hmi/TRACE.json
git commit -m "hmi: full IGDT scene graph, dynamic runtime state, 29-layer walk, and rect/matrix draw"
```

---

### Task 3: IGDT Touch Hit-Testing Queue, Full `igdt_wrapper` API, and `VReflash` Render Thread

**Files:**
- Create: `src/hmi/display/igdt/igdt_touch.h`, `src/hmi/display/igdt/igdt_touch.cpp`, `src/hmi/tests/test_igdt_touch.cpp`
- Modify: `src/hmi/display/igdt/extend/igdt_wrapper.h`, `src/hmi/display/igdt/extend/igdt_wrapper.cpp`, `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IgdtSkinTables`, `IgdtRuntime`, `IgdtSceneDrawLayers`, `IgdtGlInit`, `IgdtSwap`, `IgdtTextInit`, `IgdtSetRuntimeText`, `IgdtSetRuntimeTextId`, `hmi_wrap_cpp`.
- Produces:
  - `src/hmi/display/igdt/igdt_touch.h` (portable, host-testable):
    - `enum { IGDT_TOUCH_MOVE = 0, IGDT_TOUCH_PRESS = 1, IGDT_TOUCH_RELEASE = 3, IGDT_TOUCH_CANCEL_OUTSIDE = 0x80000000u, IGDT_TOUCH_CANCEL_PAGE = 0x40000000u };`
    - `struct IgdtTouchEvent { int32_t x; int32_t y; uint32_t type; };`
    - `typedef void (*IgdtTouchCallback)(uint32_t node_tag, const IgdtTouchEvent& ev, void* ctx);`
    - `struct IgdtTouchState { ... };` (60-entry ring buffer `0x324aec`, captured node tag/rect/screen `DAT_00df28c4..DAT_00df28d0`)
    - `void IgdtTouchInit(IgdtTouchState& ts);`
    - `bool IgdtTouchEnqueue(IgdtTouchState& ts, const IgdtTouchEvent& ev);` (`// 0x00324aec`)
    - `void IgdtTouchInvalidateForScreen(IgdtTouchState& ts, uint8_t screen_id);` (called by `hmi_wrapper_igdt_insert_page` `0x610a8` — sets captured rect to `(0, 0, -2, -2)` so next event gets `IGDT_TOUCH_CANCEL_PAGE`, Review Focus 4!)
    - `void IgdtTouchProcessQueue(IgdtTouchState& ts, const IgdtSkinTables& t, const IgdtRuntime& rt, IgdtTouchCallback cb, void* ctx);` (`// 0x00324b90`)
  - `src/hmi/display/igdt/extend/igdt_wrapper.h`:
    - `void hmi_wrapper_igdt_init();` (`0x60e3c`)
    - `void hmi_wrapper_igdt_show();` (`0x60f54` — drains touch queue + if dirty renders `IgdtSceneDrawLayers` + `IgdtSwap`)
    - `void hmi_wrapper_igdt_insert_page(uint8_t screen_id, int mode, void* page);` (`0x610a8`)
    - `void hmi_wrapper_igdt_remove_page(uint8_t screen_id, int mode);` (`0x611bc`)
    - `void hmi_wrapper_igdt_touch_event(int32_t x, int32_t y, int raw_type);` (`0x612d4`: raw `3`->press `1`, `4`->move `0`, `5`->release `3`; manages `b_hardkey_permit`/`b_touch_permit`)
    - `void hmi_wrapper_igdt_set_touch_callback(IgdtTouchCallback cb, void* ctx);`
    - Mutex-protected wrappers matching stock `0x61510..0x61b70`: `hmi_wrapper_igdt_set_text_id(id, str_id)` (`0x615ec`), `hmi_wrapper_igdt_set_text_wstr(id, wstr)` (`0x61510`), `hmi_wrapper_igdt_set_text_utf8(id, utf8)`, `hmi_wrapper_igdt_set_lang(lang)` (`0x6125c`), `hmi_wrapper_igdt_get_lang()` (`0x612cc`), `hmi_wrapper_igdt_set_branch(tag, idx)` (`0x6167c`), `hmi_wrapper_igdt_set_state(tag, idx)` (`0x616a8`), `hmi_wrapper_igdt_set_touch_area(tag, rect)` (`0x61700`), `hmi_wrapper_igdt_get_touch_area(tag, rect)` (`0x619b0`), `hmi_wrapper_igdt_set_color(tag, argb)` (`0x61758`), `hmi_wrapper_igdt_set_alpha(tag, alpha)` (`0x61784`), `hmi_wrapper_igdt_set_x(tag, x)` (`0x61840`), `hmi_wrapper_igdt_set_y(tag, y)` (`0x6186c`), `hmi_wrapper_igdt_set_xy(tag, x, y)` (`0x617dc`/`0x61808`), `hmi_wrapper_igdt_get_xy(tag, &x, &y)` (`0x618c4`), `hmi_wrapper_igdt_set_scale(tag, sx, sy)` (`0x61b3c`), `hmi_wrapper_igdt_set_matrix(tag, m)` (`0x61b10`), `hmi_wrapper_igdt_reset_node(tag)` (`0x61a10`).
    - Permit flags: `bool hmi_wrapper_igdt_get_hardkey_permit();` (`0x62a28`), `void hmi_wrapper_igdt_set_hardkey_permit(bool)` (`0x62d40`), `bool hmi_wrapper_igdt_get_mutihardkey_permit();` (`0x62d50`), `void hmi_wrapper_igdt_set_mutihardkey_permit(bool)` (`0x62d60`), `bool hmi_wrapper_igdt_get_touch_permit();` (`0x62d70`).
    - `class VReflash : public QObject` (`Q_OBJECT`, `0x62e60..0x632bc`) running a 20 ms `QTimer` on its own `QThread` calling `hmi_wrapper_igdt_show()` (and `VReflash::Instance()` `0x62f14`).

- [ ] **Step 1: Write failing host test `src/hmi/tests/test_igdt_touch.cpp`**

```cpp
#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_touch.h"
#include "tests/test_runner.h"

#include <vector>

struct RecordedTouch {
    uint32_t tag;
    int32_t x, y;
    uint32_t type;
};

static void RecordCb(uint32_t tag, const IgdtTouchEvent& ev, void* ctx) {
    std::vector<RecordedTouch>* out = static_cast<std::vector<RecordedTouch>*>(ctx);
    RecordedTouch r = {tag, ev.x, ev.y, ev.type};
    out->push_back(r);
}

#ifdef HMI_HAVE_IGDT_TABLES
static void test_appnoconn_info_button_hit_and_release() {
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    IgdtLayerShowScreen(rt, g_igdtTablesVw, 0x04);
    // VBtnInfo touch area on screen 0x04 (from vAppNoConnection 0x33a660).
    CHECK_EQ(IgdtSetTouchArea(rt, 0x40100166u, 0, 396, 800, 84), IGDT_OK);

    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEvent up = {400, 438, IGDT_TOUCH_RELEASE};
    CHECK(IgdtTouchEnqueue(ts, down));
    CHECK(IgdtTouchEnqueue(ts, up));
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, rt, RecordCb, &log);

    CHECK_EQ(log.size(), 2u);
    CHECK_EQ(log[0].tag, 0x40100166u);
    CHECK_EQ(log[0].type, (uint32_t)IGDT_TOUCH_PRESS);
    CHECK_EQ(log[1].tag, 0x40100166u);
    CHECK_EQ(log[1].type, (uint32_t)IGDT_TOUCH_RELEASE);
}

static void test_drag_outside_cancels_release() {
    // Review Focus 3: sliding finger off button sets 0x80000000 cancel bit.
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    IgdtLayerShowScreen(rt, g_igdtTablesVw, 0x04);
    CHECK_EQ(IgdtSetTouchArea(rt, 0x40100166u, 0, 396, 800, 84), IGDT_OK);

    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEvent move_out = {400, 100, IGDT_TOUCH_MOVE};
    IgdtTouchEvent up_out = {400, 100, IGDT_TOUCH_RELEASE};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchEnqueue(ts, move_out);
    IgdtTouchEnqueue(ts, up_out);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, rt, RecordCb, &log);

    CHECK_EQ(log.size(), 3u);
    CHECK((log[1].type & IGDT_TOUCH_CANCEL_OUTSIDE) != 0);
    CHECK_EQ(log[2].type, (uint32_t)(IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_OUTSIDE));
}

static void test_page_insert_cancels_captured_touch() {
    // Review Focus 4: inserting screen while touch is captured sets 0x40000000.
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    IgdtLayerShowScreen(rt, g_igdtTablesVw, 0x04);
    CHECK_EQ(IgdtSetTouchArea(rt, 0x40100166u, 0, 396, 800, 84), IGDT_OK);

    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, rt, RecordCb, &log);
    CHECK_EQ(log.size(), 1u);

    IgdtTouchInvalidateForScreen(ts, 0x04);
    IgdtTouchEvent up = {400, 438, IGDT_TOUCH_RELEASE};
    IgdtTouchEnqueue(ts, up);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, rt, RecordCb, &log);
    CHECK_EQ(log.size(), 2u);
    CHECK_EQ(log[1].type, (uint32_t)(IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_PAGE));
}

static void test_queue_full_drops_without_overflow() {
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    IgdtTouchEvent ev = {10, 10, IGDT_TOUCH_MOVE};
    for (int i = 0; i < 60; ++i) CHECK(IgdtTouchEnqueue(ts, ev));
    CHECK(!IgdtTouchEnqueue(ts, ev));
}
#endif

int main() {
#ifdef HMI_HAVE_IGDT_TABLES
    RUN_TEST(test_appnoconn_info_button_hit_and_release);
    RUN_TEST(test_drag_outside_cancels_release);
    RUN_TEST(test_page_insert_cancels_captured_touch);
    RUN_TEST(test_queue_full_drops_without_overflow);
#endif
    return test_summary();
}
```

Register `test_igdt_touch` in `src/hmi/tests/CMakeLists.txt`.

- [ ] **Step 2: Run host tests to verify failure**

Run: `./tools/build_hmi.sh --host-tests`
Expected: FAIL (`igdt_touch.h` not found).

- [ ] **Step 3: Implement `igdt_touch.{h,cpp}`**

Implement `src/hmi/display/igdt/igdt_touch.cpp` directly from `0x324aec` and `0x324b90`:
- Ring buffer of `60` (`0x3c`) `IgdtTouchEvent` entries with `head`, `tail`, `count`.
- On dequeue when a target is captured (`captured_tag != 0`):
  - If `(x, y)` is outside `[cap_x0..cap_x1] x [cap_y0..cap_y1]`:
    - If `cap_x0 == 0 && cap_y0 == 0 && cap_x1 == -2 && cap_y1 == -2` (page-change sentinel from `0x610a8`), OR `ev.type |= IGDT_TOUCH_CANCEL_PAGE`.
    - Else collapse captured rect to `(0, 0, -1, -1)` and OR `ev.type |= IGDT_TOUCH_CANCEL_OUTSIDE`.
  - Invoke `cb(captured_tag, ev, ctx)`; if `(ev.type & 0xff) == IGDT_TOUCH_RELEASE`, clear `captured_tag = 0`, `captured_screen = 0xff`.
- On dequeue when no target is captured and `(ev.type & 0xff) == IGDT_TOUCH_PRESS`:
  - Iterate layers `28` down to `0` (`rt.layers[layer]`). For each active `sid != 0xff`, iterate `scr.touch_first .. scr.touch_first + scr.touch_count`.
  - Traverse the path's byte steps from `scr.root_first` accumulating dynamic group origin `(ox, oy)` and respecting live `rt.branch[node.eidx]` (rejecting paths traversing an inactive branch child!).
  - Compute hit rect of the resolved dynamic group node from `rt.dyn_grp[idx].touch_clip` (skipping empty rect `w <= 0 || h <= 0`) offset by `(ox, oy)`.
  - First hit captures `captured_tag = node.tag`, `captured_screen = sid`, `cap_x0..cap_y1`, and invokes `cb(node.tag, ev, ctx)`.

- [ ] **Step 4: Expand `igdt_wrapper.{h,cpp}` and add `VReflash` (`Q_OBJECT`)**

1. Add recursive `pthread_mutex_t` (`0x00a0fa40` per `0x320c7c`) protecting all `IgdtGetRuntime()` and `IgdtTouchState` accesses in `igdt_wrapper.cpp`.
2. Wire `hmi_wrapper_igdt_init()` (`0x60e3c`) to initialize the mutex and call `IgdtRuntimeInit(IgdtGetRuntime(), g_igdtTablesVw)` + `IgdtTouchInit(g_touchState)`.
3. Wire `hmi_wrapper_igdt_show()` (`0x60f54`): on first call init GL + FreeType; on every call drain `IgdtTouchProcessQueue` under mutex (invoking callback outside or safely with recursive mutex), then if `IgdtConsumeDirty()` is true, draw `IgdtSceneDrawLayers(g_igdtTablesVw, rt)` and `IgdtSwap()`.
4. Implement `hmi_wrapper_igdt_insert_page` (`0x610a8`), `hmi_wrapper_igdt_remove_page` (`0x611bc`), `hmi_wrapper_igdt_touch_event` (`0x612d4`), all setter/getter wrappers (`0x61510..0x61b70`), permit flags (`0x62a28..0x62d70`), and `VReflash` (`0x62e60..0x632bc`, `hmi_wrap_cpp` on `igdt_wrapper.h`).

- [ ] **Step 5: Run host tests and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: All host tests including `test_igdt_touch` PASS; `build/hmi/hmi` builds and passes ABI + Qt-import + trace gates.

- [ ] **Step 6: Commit**

```bash
git add src/hmi/display/igdt/igdt_touch.h src/hmi/display/igdt/igdt_touch.cpp \
        src/hmi/display/igdt/extend/igdt_wrapper.h src/hmi/display/igdt/extend/igdt_wrapper.cpp \
        src/hmi/tests/test_igdt_touch.cpp src/hmi/tests/CMakeLists.txt \
        src/hmi/CMakeLists.txt src/hmi/TRACE.json
git commit -m "hmi: IGDT touch hit-testing queue, full igdt_wrapper API, and VReflash thread"
```

---

### Task 4: `VmfThread` and `VBaseProxy` VMF IPC Core

**Files:**
- Create: `src/hmi/service/vmf/vmf_api.h`, `src/hmi/service/vmf/vmf_frame.h`, `src/hmi/service/vmf/vmf_frame.cpp`, `src/hmi/service/vmf/VmfThread.h`, `src/hmi/service/vmf/VmfThread.cpp`, `src/hmi/service/utility/vbaseproxy.h`, `src/hmi/service/utility/vbaseproxy.cpp`, `src/hmi/tests/test_vmf_core.cpp`
- Modify: `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `rcd330_vmf_client` (`libvmf_client.so`), `osal_api.h`.
- Produces:
  - `src/hmi/service/vmf/vmf_api.h`:
    ```cpp
    extern "C" {
    int nw_vmf_connect(const char* name, int arg1, int arg2, short timeout_ms, unsigned short buf_size);
    int nw_vmf_register_msg_group(int handle, unsigned short group_count, const unsigned char* groups);
    int nw_vmf_send(char handle, const void* pkt);
    int nw_vmf_receive(char handle, void* pkt, unsigned short timeout_ms);
    }
    ```
  - `src/hmi/service/vmf/vmf_frame.h` (portable, host-testable):
    - `enum { VMF_PKT_SIZE = 0x4d0, VMF_HDR_SIZE = 0x1a, VMF_MAX_PAYLOAD = 0x4b4 };`
    - `bool VmfPackMsg(uint8_t out_pkt[VMF_PKT_SIZE], const uint8_t* raw_buf, uint32_t raw_len);` (`// 0x0005fe68`)
    - `struct VmfUnpacked { uint8_t group; uint8_t sub; uint16_t len; const uint8_t* payload; };`
    - `bool VmfUnpackMsg(const uint8_t pkt[VMF_PKT_SIZE], VmfUnpacked* out);`
    - `class IVmfListener { public: virtual ~IVmfListener() {} virtual void OnVmfPacket(const uint8_t* pkt) = 0; };`
    - `class VmfRouter` (stores unique registered groups + `(listener, group)` list; `AddGroup`, `AddListener`, `Dispatch`).
  - `src/hmi/service/vmf/VmfThread.h`:
    - `class VmfThread : public QThread` (`Q_OBJECT`, `0x602e0`, `static VmfThread* Instance()` `0x60384`, `bool Connect()` `0x60428`, `void start()` `0x604b8`, `void AddListener(IVmfListener*, uint8_t grp)` `0x604fc`, `bool AddGroup(uint8_t grp)` `0x60720`, `void Dispatch(const uint8_t* pkt)` `0x605f0`, `bool Send(const uint8_t* pkt)` `0x606e8`, `void run()`).
  - `src/hmi/service/utility/vbaseproxy.h`:
    - `class VBaseProxy : public IVmfListener` (`0x5fdb4`, `void RegisterVmfGroup(const uint8_t* groups, uint32_t count)` `0x5fdd4`, `bool PackMsg(uint8_t out[VMF_PKT_SIZE], const uint8_t* buf, uint32_t len)` `0x5fe68`, `void SendVmfMsg(const uint8_t* buf, uint32_t len)` `0x5ff4c`, `void OnVmfPacket(const uint8_t* pkt)` dispatching to `virtual void RecieveVmfMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len) = 0;`).

- [ ] **Step 1: Write failing host test `src/hmi/tests/test_vmf_core.cpp`**

```cpp
#include "service/vmf/vmf_frame.h"
#include "tests/test_runner.h"

#include <string.h>
#include <vector>

static void test_pack_valid_wake_msg() {
    // VPowerProxy::SendWakeStateToPowerAgent(1): [grp=0x0b, sub=0x01, len_hi=0x00, len_lo=0x02, 0x6e, 0x01]
    const uint8_t raw[6] = {0x0b, 0x01, 0x00, 0x02, 0x6e, 0x01};
    uint8_t pkt[VMF_PKT_SIZE];
    CHECK(VmfPackMsg(pkt, raw, sizeof(raw)));
    CHECK_EQ(pkt[0], 0x02);
    CHECK_EQ(pkt[0x14], 0x0b);
    CHECK_EQ(pkt[0x15], 0x01);
    CHECK_EQ((uint16_t)(pkt[0x18] | (pkt[0x19] << 8)), 2u);
    CHECK_EQ(pkt[0x1a], 0x6e);
    CHECK_EQ(pkt[0x1b], 0x01);

    VmfUnpacked u;
    CHECK(VmfUnpackMsg(pkt, &u));
    CHECK_EQ(u.group, 0x0b);
    CHECK_EQ(u.sub, 0x01);
    CHECK_EQ(u.len, 2u);
    CHECK_EQ(u.payload[0], 0x6e);
    CHECK_EQ(u.payload[1], 0x01);
}

static void test_pack_rejects_malformed_inputs() {
    // Review Focus 2: NULL, <4 bytes, length mismatch, >0x4b4 payload overflow.
    uint8_t pkt[VMF_PKT_SIZE];
    CHECK(!VmfPackMsg(pkt, 0, 6));
    const uint8_t tiny[3] = {0x0b, 0x01, 0x00};
    CHECK(!VmfPackMsg(pkt, tiny, 3));
    const uint8_t mismatch[6] = {0x0b, 0x01, 0x00, 0x05, 0x6e, 0x01};
    CHECK(!VmfPackMsg(pkt, mismatch, 6));
    uint8_t huge[4 + VMF_MAX_PAYLOAD + 1];
    memset(huge, 0, sizeof(huge));
    huge[0] = 0x8f;
    huge[1] = 0x24;
    uint16_t bad_len = VMF_MAX_PAYLOAD + 1;
    huge[2] = (uint8_t)(bad_len >> 8);
    huge[3] = (uint8_t)(bad_len & 0xff);
    CHECK(!VmfPackMsg(pkt, huge, sizeof(huge)));
    // Zero-length payload is valid.
    const uint8_t zero_payload[4] = {0x0b, 0x05, 0x00, 0x00};
    CHECK(VmfPackMsg(pkt, zero_payload, 4));
    CHECK_EQ(pkt[0], 0x02);
    CHECK_EQ(pkt[0x14], 0x0b);
    CHECK_EQ((uint16_t)(pkt[0x18] | (pkt[0x19] << 8)), 0u);
}

struct MockListener : public IVmfListener {
    std::vector<uint8_t> seen_subs;
    void OnVmfPacket(const uint8_t* pkt) {
        VmfUnpacked u;
        if (VmfUnpackMsg(pkt, &u)) seen_subs.push_back(u.sub);
    }
};

static void test_router_group_dedup_and_fanout() {
    VmfRouter r;
    MockListener a, b;
    CHECK(r.AddGroup(0x70));
    CHECK(!r.AddGroup(0x70));  // duplicate ignored like 0x60720
    CHECK(r.AddGroup(0x0c));
    CHECK_EQ(r.Groups().size(), 2u);
    r.AddListener(&a, 0x70);
    r.AddListener(&b, 0x70);
    r.AddListener(&b, 0x0c);

    uint8_t pkt[VMF_PKT_SIZE];
    const uint8_t raw[5] = {0x70, 0x42, 0x00, 0x01, 0xaa};
    CHECK(VmfPackMsg(pkt, raw, sizeof(raw)));
    r.Dispatch(pkt);
    CHECK_EQ(a.seen_subs.size(), 1u);
    CHECK_EQ(a.seen_subs[0], 0x42);
    CHECK_EQ(b.seen_subs.size(), 1u);
    CHECK_EQ(b.seen_subs[0], 0x42);
}

int main() {
    RUN_TEST(test_pack_valid_wake_msg);
    RUN_TEST(test_pack_rejects_malformed_inputs);
    RUN_TEST(test_router_group_dedup_and_fanout);
    return test_summary();
}
```

Register `test_vmf_core` in `src/hmi/tests/CMakeLists.txt`.

- [ ] **Step 2: Run host test to verify failure**

Run: `./tools/build_hmi.sh --host-tests`
Expected: FAIL (`service/vmf/vmf_frame.h` not found).

- [ ] **Step 3: Implement `vmf_api.h`, `vmf_frame.{h,cpp}`, `VmfThread.{h,cpp}`, `vbaseproxy.{h,cpp}`**

1. `vmf_frame.cpp`: implement `VmfPackMsg` (`// 0x0005fe68`) matching stock's exact checks (`raw_buf != NULL`, `raw_len >= 4`, `payload_len = (raw[2] << 8) | raw[3]`, `payload_len <= 0x4b4 && payload_len == raw_len - 4`, `out[0] = 2`, `out[0x14] = grp`, `out[0x15] = sub`, little-endian `uint16_t` at `+0x18`, `memcpy` to `+0x1a`), `VmfUnpackMsg` validating `pkt != NULL && pkt[0] == 2` and `len <= VMF_MAX_PAYLOAD`, and `VmfRouter`.
2. `VmfThread.cpp`:
   - `VmfThread::VmfThread()` (`// 0x000602e0`) leaves the client name string empty (`.bss` `0x97021c`, matching stock's default empty `std::string`, assignable via `SetName` `// 0x000603f4`).
   - `VmfThread::Instance()` (`// 0x00060384`) Meyer's singleton.
   - `VmfThread::Connect()` (`// 0x00060428`) loops up to 10 attempts calling `nw_vmf_connect(name, 0, 0, 100, 0xfe0)`, sleeping `QThread::msleep(2)` between retries; on handle `>= 0` calls `nw_vmf_register_msg_group(handle, groups.size(), &groups[0])`.
   - `VmfThread::start()` (`// 0x000604b8`) calls `Connect()`; on failure logs error and calls `abort()` / `__assert_fail("0", "../service/vmf/VmfThread.cpp", 0xd7, "void VmfThread::start()")`, otherwise `QThread::start(QThread::InheritPriority)`.
   - `VmfThread::run()` loops while `!m_stop` calling `nw_vmf_receive(m_handle, buf, 1000)`; when return `>= 0`, calls `Dispatch(buf)` (`// 0x000605f0`) locking `m_mutex` (`QMutex`).
   - `VmfThread::Send()` (`// 0x000606e8`) checks `m_handle != -1` and calls `nw_vmf_send(m_handle, pkt)`.
3. `vbaseproxy.cpp`: implements `RegisterVmfGroup` (`// 0x0005fdd4`), `PackMsg` (`// 0x0005fe68`), `SendVmfMsg` (`// 0x0005ff4c`, skipping `Send` when `PackMsg` returns false — Review Focus 2!), and `OnVmfPacket` unpacking `(group, sub, payload, len)` into `RecieveVmfMsg`.
4. Add `VmfThread.h` to `hmi_wrap_cpp` and link `rcd330_vmf_client` in `src/hmi/CMakeLists.txt`.

- [ ] **Step 4: Run host tests and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: `test_vmf_core` PASS; `build/hmi/hmi` links `libvmf_client.so` cleanly and passes all ABI/DT_NEEDED/Qt/trace checks.

- [ ] **Step 5: Commit**

```bash
git add src/hmi/service/vmf/ src/hmi/service/utility/vbaseproxy.h src/hmi/service/utility/vbaseproxy.cpp \
        src/hmi/tests/test_vmf_core.cpp src/hmi/tests/CMakeLists.txt src/hmi/CMakeLists.txt src/hmi/TRACE.json
git commit -m "hmi: VmfThread and VBaseProxy VMF framing/routing core with host tests"
```

---

### Task 5: Subsystem Proxies (`VPowerProxy`, `VDisplayProxy`, `VAppConnectProxy`, and Boot/Status-Bar Proxies)

**Files:**
- Create:
  - `src/hmi/control_logic/subsystem/proxy_decoders.h`, `src/hmi/control_logic/subsystem/proxy_decoders.cpp` (portable, host-testable VMF payload decoders)
  - `src/hmi/control_logic/subsystem/power/vpowerproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/display/vdisplayproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/appconnect/vappconnectproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/audio/vaudioproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/bap/vbapproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/amfm/vamfmproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/media/vmediaproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/system_time_calendar/vsystemtimecalendarproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/pss/vpssproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/diag/vdiagproxy.{h,cpp}`
  - `src/hmi/control_logic/subsystem/phase3_stubs.{h,cpp}`
  - `src/hmi/tests/test_proxies.cpp`
- Modify: `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `VBaseProxy`, `VmfThread`, `osal_api.h`.
- Produces:
  - Portable decoders (`proxy_decoders.h`):
    - `struct PowerDevStatus { uint8_t acc_status; uint8_t ignition_status; };`
    - `bool DecodePowerDevStatus(const uint8_t* data, uint16_t len, PowerDevStatus* out);` (`0x0C:0x04`)
    - `bool DecodePowerModeChange(const uint8_t* data, uint16_t len, uint8_t* mode_out);` (`0x0C:0x01`)
    - `bool DecodeVipAudioMode(const uint8_t* data, uint16_t len, uint8_t* audio_mode_out);` (`0x74:0x01`)
    - `struct AppConnectCoding { uint32_t region_mode; uint32_t sub_mode; bool cp, aa, ml, cl; };`
    - `bool DecodeAppConnectPss(const uint8_t* data, uint16_t len, AppConnectCoding* state);` (`0x72:0x04` / `0x8D:0x04`, per `0x34ac5c` & `0x3474a8`)
    - `struct BapExtTemp { int16_t temp_half_c; uint8_t unit_f; bool invalid; bool night_mode; };`
    - `bool DecodeBapEnvStatus(const uint8_t* data, uint16_t len, BapExtTemp* out);` (`0xA2:0x05`)
    - `struct AudioVolumeStatus { uint8_t active, ent, phone, ring, sds; bool muted; };`
    - `bool DecodeAudioVolumeOrMute(const uint8_t* data, uint16_t len, AudioVolumeStatus* state);` (`0x3F:0x01`)
  - Proxy singletons matching stock class/method names and VMF groups (`docs/hmi-internals.md` §4):
    - `VPowerProxy::Instance()` (`0x55fd4`): `Init()` (`0x5601c`), `SendWakeStateToPowerAgent(int)` (`0x56040`), `SetBacklightOnOFF(uint8_t)` (`0x567a0`), `SetHMIOn()` (`0x56950`), `SetHMIOff()` (`0x568cc`), `GetPlatformType()` (`0x5a0a0`), `IsHMIOn()` (`0x5a0b8`), `SetRvcCoded(uint8_t)` (`0x5a394`), `SetStartupDone(uint8_t)` (`0x5a3ec`), `GetAuxMode()` (`0x563bc`), `GetPowerState()` (`0x5a068`), `GetTimeDisplayFlags(uint8_t*, uint8_t*, uint8_t*)` (`0x5a070`), `RecieveVmfMsg` (`0x5bab4`).
    - `VDisplayProxy::Instance()` (`0x52174`): `Init()` (`0x531a8`), `SetMainPage(int audio_src, int force)` (`0x54fcc`), `InsertPage(int page_id)` (`0x54314`), `PushPage(uint8_t scr_id)` (`0x530e4`), `ClosePage(uint8_t scr_id)` (`0x537fc` / `0x53b98`), `RemovePage(uint8_t scr_id)` (`0x515f8`), `GetLanguage()` (`0x50ad0`), `SetDisplaySink(IDisplaySink*)` (wired by `VDisplayTask` in Task 9 so `VDisplayProxy` stays decoupled from `VDisplayTask`!).
    - `VAppConnectProxy::Instance()` (`0x34860c`): `Init()` (`0x346fb4`), `QueryPssCoding()` (`0x3476a8`), `IsCoded()` (`0x347224`), `GetRegionMode()` (`0x347498`), `GetAvailableApps(uint8_t* ml, uint8_t* aa, uint8_t* cp)` (`0x347150`), `GetAvailableAppsRow(uint8_t* cl, uint8_t* aa, uint8_t* cp)` (`0x347180`), `RecieveVmfMsg` (`0x34ac5c`).
    - `VAudioProxy::Instance()` (`0x46b38`), `VBAPProxy::Instance()` (`0xafb84`), `VAMFMProxy::Instance()` (`0xad0a8`), `VMediaProxy::Instance()` (`0xd82e0`), `VSystemTimeCalendarProxy::Instance()` (`0x2021d0`), `VPSSProxy::Instance()` (`0x1c338c`), `VDiagProxy::Instance()` (`0x1ffc10`), plus Phase-3 group registration shells (`VBTProxy`, `VMirrorLinkProxy`, `VCarPlayProxy`, `VCarLifeProxy`, `VAndroidAutoProxy`, `VServiceProxy`, `VSubsystemProxy`).

- [ ] **Step 1: Write failing host test `src/hmi/tests/test_proxies.cpp`**

```cpp
#include "control_logic/subsystem/proxy_decoders.h"
#include "tests/test_runner.h"

static void test_power_decoders() {
    PowerDevStatus dev = {0, 0};
    const uint8_t dev_bytes[5] = {0x05, 0x05, 0x00, 0x00, 0x00};  // CL_S=1, CL_15=1
    CHECK(DecodePowerDevStatus(dev_bytes, 5, &dev));
    CHECK_EQ(dev.acc_status, 1);
    CHECK_EQ(dev.ignition_status, 1);
    CHECK(!DecodePowerDevStatus(dev_bytes, 0, &dev));

    uint8_t mode = 0;
    const uint8_t mode_run[1] = {3};
    CHECK(DecodePowerModeChange(mode_run, 1, &mode));
    CHECK_EQ(mode, 3);

    uint8_t audio_mode = 0;
    const uint8_t vip_radio[2] = {0x81, 0x10};
    CHECK(DecodeVipAudioMode(vip_radio, 2, &audio_mode));
    CHECK_EQ(audio_mode, 0x10);
}

static void test_appconnect_pss_decoder() {
    AppConnectCoding ac = {1, 0, false, false, false, false};
    // pData[2] == 0 sets region_mode = pData[3], sub_mode = pData[5] (0x34ac5c).
    const uint8_t pkt0[6] = {0, 4, 0, 2, 0, 1};
    CHECK(DecodeAppConnectPss(pkt0, 6, &ac));
    CHECK_EQ(ac.region_mode, 2u);
    CHECK_EQ(ac.sub_mode, 1u);
    // pData[2] == 1 carries protocol availability flags (0x3474a8).
    const uint8_t pkt1[6] = {0, 5, 1, 1, 1, 1};
    CHECK(DecodeAppConnectPss(pkt1, 6, &ac));
    CHECK(ac.cp || ac.aa || ac.ml);
    CHECK(!DecodeAppConnectPss(pkt0, 3, &ac));
}

static void test_bap_and_audio_decoders() {
    BapExtTemp env = {0, 0, true, false};
    // 20.5 C => (20.5 + 50) * 2 = 141; unit=0 (C), valid=0, night=1.
    const uint8_t a2_05[7] = {100, 141, 0, 0, 0, 1, 0};
    CHECK(DecodeBapEnvStatus(a2_05, 7, &env));
    CHECK_EQ(env.temp_half_c, 41);
    CHECK_EQ(env.unit_f, 0);
    CHECK(!env.invalid);
    CHECK(env.night_mode);

    AudioVolumeStatus av = {0, 0, 0, 0, 0, false};
    const uint8_t vol[6] = {0x00, 12, 12, 10, 8, 9};
    CHECK(DecodeAudioVolumeOrMute(vol, 6, &av));
    CHECK_EQ(av.active, 12);
    const uint8_t mute[5] = {0x04, 1, 1, 0, 0};
    CHECK(DecodeAudioVolumeOrMute(mute, 5, &av));
    CHECK(av.muted);
}

int main() {
    RUN_TEST(test_power_decoders);
    RUN_TEST(test_appconnect_pss_decoder);
    RUN_TEST(test_bap_and_audio_decoders);
    return test_summary();
}
```

Register `test_proxies` in `src/hmi/tests/CMakeLists.txt`.

- [ ] **Step 2: Run host tests to verify failure**

Run: `./tools/build_hmi.sh --host-tests`
Expected: FAIL (`proxy_decoders.h` not found).

- [ ] **Step 3: Implement `proxy_decoders.{h,cpp}` and all subsystem proxies**

1. Verify `VAppConnectProxy` `0x3474a8` payload byte offsets from `docs/decompiled/hmi/functions/0x3474a8.c` and `VPowerProxy` `0x5bab4` sub-IDs (`0x0C` `PA_OUT_MODE_CHG`, `PA_OUT_DEV_STS`, `PA_OUT_WAKE_UP_REASON`, `PA_OUT_VRM_INFO`; `0x74:0x01` VIP audio mode `0x81`) and `VBAPProxy` `0xA2:0x05` / `0x70:0x01` from `docs/decompiled/hmi/functions/` before writing `proxy_decoders.cpp`.
2. Implement `VPowerProxy`:
   - Subscribes to `{0x6e, 0x0d, 0x0e, 0x74, 0x0c}` (`0x969958`).
   - `SendWakeStateToPowerAgent(1)` (`// 0x00056040`) sends `{0x0b, 0x01, 0x00, 0x02, 0x6e, 0x01}` and logs stock banner.
   - On `PA_OUT_MODE_CHG` (`3=RUN` / `6=TEMP_ON`) calls `SetHMIOn()` (`// 0x00056950`), which sends display-on to VIP (`0x567a0`) and calls `VDisplayProxy::Instance()->SetMainPage(VAudioProxy::Instance()->GetAudioMode(), 0)`.
   - On `0x74:0x01` audio mode notification (`0x81`), dispatches a `FILTER_MSG` into `VControlTask` -> `VPowerNotifyFilter::HandleAudioModeStatus` (`0x46054`).
3. Implement `VDisplayProxy` (`0x52174`, `0x531a8`, `0x54fcc`, `0x54314`, `0x530e4`, `0x537fc`, `0x515f8`) mapping stock logical main-page IDs (`1` = Tuner -> screen `0xf9`, `13`/`0x0d` or AppConnect -> screen `0x04`, etc., verifying lookup table `0x003f3ac4` in stock binary via `hmi_elf.py`!) and forwarding `SigShowPage(scr_id)` / `SigClosePage(scr_id)` to its sink (`VDisplayTask`).
4. Implement `VAppConnectProxy` (`0x34860c`, `0x346fb4`, `0x3476a8`, `0x34ac5c`, `0x347224`, `0x347498`, `0x347150`, `0x347180`): sends PSS read `{0x8c, 0x08, 0x00, 0x03, 0x00, 0x72, 0x04}` on boot, parses `0x72:0x04`/`0x8D:0x04` coding reply, and defaults safely to stock's initial ctor state (`region_mode = 1`, `0x3482f8`) until the PSS reply arrives.
5. Implement boot/status-bar proxies (`VAudioProxy`, `VBAPProxy`, `VAMFMProxy`, `VMediaProxy`, `VSystemTimeCalendarProxy`, `VPSSProxy`, `VDiagProxy`) and `phase3_stubs.{h,cpp}` registering the exact stock VMF groups from `docs/hmi-internals.md` §4 so `VmfThread::Connect` registers the complete stock group list with `opt/vmf` on boot.
6. Register all `.cpp` files in `src/hmi/CMakeLists.txt`.

- [ ] **Step 4: Run host tests and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: `test_proxies` PASS; `build/hmi/hmi` builds and passes ABI + Qt + trace checks.

- [ ] **Step 5: Commit**

```bash
git add src/hmi/control_logic/subsystem/ src/hmi/tests/test_proxies.cpp \
        src/hmi/tests/CMakeLists.txt src/hmi/CMakeLists.txt src/hmi/TRACE.json
git commit -m "hmi: VPowerProxy, VDisplayProxy, VAppConnectProxy, and boot/status-bar proxies"
```

---

### Task 6: Notify Filter Chain and `VControlTask`

**Files:**
- Create:
  - `src/hmi/control_logic/mode/vbasefilter.{h,cpp}`
  - `src/hmi/control_logic/mode/power/vpowernotifyfilter.{h,cpp}`
  - `src/hmi/control_logic/mode/display/vdisplaycmdfilter.{h,cpp}`
  - `src/hmi/control_logic/mode/ipcl/vipclnotifyfilter.{h,cpp}`
  - `src/hmi/control_logic/mode/audio/vaudionotifyfilter.{h,cpp}`
  - `src/hmi/control_logic/mode/media/vmedianotifyfilter.{h,cpp}`
  - `src/hmi/control_logic/mode/camera/vcameranotifyfilter.{h,cpp}`
  - `src/hmi/control_logic/vcontroltask.{h,cpp}`
  - `src/hmi/tests/test_filters.cpp`
- Modify: `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `VBaseTask`, all proxies from Task 5, `hmi_wrap_cpp`.
- Produces:
  - `struct FILTER_MSG { uint8_t valid; uint32_t type; uint8_t para[64]; };` (stock `FILTER_MSG` registered via `QMetaType::registerType` in `0x3399c`; portable subset for filter chain).
  - `class VBaseFilter`: `bool AddNextFilter(VBaseFilter* next)` (`// 0x00039780`, deduplicates pointer), `virtual FILTER_MSG HandleMsg(const FILTER_MSG& msg)`.
  - `class IFilterActions` (pure virtual action sink injected into `VPowerNotifyFilter`, `VIPCLNotifyFilter`, `VDisplayCmdFilter` so the filters are unit-testable on macOS without Qt/VMF!):
    - `SetMainPage(int audio_mode, int force)`, `InsertPage(int page_id)`, `ShowScreen(uint8_t scr_id)`, `SetAudioMode(int mode)`, `QueryTunerInfo()`, `NotifyDisplayKey(uint8_t key, uint8_t state)`, `IsAppConnectAvailable()`.
  - `VPowerNotifyFilter` (`0x45ea4`, `HandleAudioModeStatus` `0x46054`, `HandlePowerEjectStatus` `0x45ec4`).
  - `VDisplayCmdFilter` (`0x40e18`, `HandleCmdMsg` `0x40e38`).
  - `VIPCLNotifyFilter` (`0x4398c`): dispatches faceplate keys `1=RADIO` (`0x42934` -> Tuner `0xf9`), `10=APP` (`0x41c40` -> App-Connect `0x04` when `IsAppConnectAvailable()`), `6=TUNE_OK` (`0x41b74`), `7=TUNE_UP` (`0x41ab8`), `8=TUNE_DOWN` (`0x41990`), and safely logs + drops Phase-3 keys `2=MEDIA`, `3=VOICE`, `4=SETUP`, `5=PHONE`, `9=EJECT` (Review Focus 5).
  - `class VControlTask : public VBaseTask` (`Q_OBJECT`, `0x2fd18` ctor building & linking the filter chain, `static VControlTask* Instance()` `0x30054`, `void Init()` `0x33c68` initializing proxies + `0x3399c` registering `FILTER_MSG` & connecting signals/slots, `void Run()` `0x30064`, `void PostKeyEvent(uint8_t code, uint8_t state)` `0x322bc`, `void PostPowerNotify(const FILTER_MSG&)`, signals `SigKeyEventNotify(uint8_t, uint8_t)`, `SigIgdtRefreshNotify(uint8_t)` `0x36270`, `SigRemoveHighestPage()`).

- [ ] **Step 1: Write failing host test `src/hmi/tests/test_filters.cpp`**

```cpp
#include "control_logic/mode/ipcl/vipclnotifyfilter.h"
#include "control_logic/mode/power/vpowernotifyfilter.h"
#include "control_logic/mode/vbasefilter.h"
#include "tests/test_runner.h"

#include <vector>

struct MockActions : public IFilterActions {
    int last_main_page;
    int last_audio_mode;
    std::vector<uint8_t> shown_screens;
    std::vector<int> key_events;
    bool app_available;

    MockActions() : last_main_page(-1), last_audio_mode(-1), app_available(true) {}
    void SetMainPage(int mode, int) { last_main_page = mode; }
    void InsertPage(int) {}
    void ShowScreen(uint8_t scr) { shown_screens.push_back(scr); }
    void SetAudioMode(int mode) { last_audio_mode = mode; }
    void QueryTunerInfo() {}
    void NotifyDisplayKey(uint8_t key, uint8_t state) { key_events.push_back((key << 8) | state); }
    bool IsAppConnectAvailable() const { return app_available; }
};

static void test_filter_chain_dedup() {
    VBaseFilter a, b;
    CHECK(a.AddNextFilter(&b));
    CHECK(!a.AddNextFilter(&b));  // duplicate rejected like 0x39780
    CHECK(!a.AddNextFilter(0));
    CHECK_EQ(a.NextCount(), 1u);
}

static void test_power_audio_mode_routes_to_tuner() {
    MockActions act;
    VPowerNotifyFilter f(&act);
    FILTER_MSG msg;
    msg.Reset();
    msg.para[0] = 0x0b;
    msg.para[1] = 0x05;
    msg.para[2] = 0x10;  // default/radio audio mode -> main page 1 (Tuner)
    f.HandleAudioModeStatus(msg);
    CHECK_EQ(act.last_audio_mode, 1);
    CHECK_EQ(act.last_main_page, 1);
}

static void test_ipcl_hardkeys_radio_app_knob_and_phase3_safety() {
    MockActions act;
    VIPCLNotifyFilter f(&act);

    // APP release (code 10, key action release/short press) -> screen 0x04.
    f.HandleHardKey(10, 0);
    CHECK_EQ(act.shown_screens.size(), 1u);
    CHECK_EQ(act.shown_screens[0], 0x04);

    // RADIO release (code 1) -> screen 0xf9.
    f.HandleHardKey(1, 0);
    CHECK_EQ(act.shown_screens.size(), 2u);
    CHECK_EQ(act.shown_screens[1], 0xf9);

    // Tune knob UP (7) and DOWN (8) forward to display key notifications.
    f.HandleHardKey(7, 1);
    f.HandleHardKey(8, 1);
    CHECK_EQ(act.key_events.size(), 2u);
    CHECK_EQ(act.key_events[0], (7 << 8) | 1);
    CHECK_EQ(act.key_events[1], (8 << 8) | 1);

    // Review Focus 5: Phase-3 keys (MEDIA=2, SETUP=4, PHONE=5) and out-of-range key (99) do not crash or change screen.
    f.HandleHardKey(2, 0);
    f.HandleHardKey(4, 0);
    f.HandleHardKey(5, 0);
    f.HandleHardKey(99, 0);
    CHECK_EQ(act.shown_screens.size(), 2u);
}

int main() {
    RUN_TEST(test_filter_chain_dedup);
    RUN_TEST(test_power_audio_mode_routes_to_tuner);
    RUN_TEST(test_ipcl_hardkeys_radio_app_knob_and_phase3_safety);
    return test_summary();
}
```

Register `test_filters` in `src/hmi/tests/CMakeLists.txt`.

- [ ] **Step 2: Run host tests to verify failure**

Run: `./tools/build_hmi.sh --host-tests`
Expected: FAIL (`vbasefilter.h` not found).

- [ ] **Step 3: Implement the filter chain and `VControlTask`**

1. Implement `vbasefilter.{h,cpp}` (`// 0x00039780 VBaseFilter::AddNextFilter`), `vpowernotifyfilter.{h,cpp}` (`// 0x00045ea4`, `// 0x00046054`, `// 0x00045ec4`), `vdisplaycmdfilter.{h,cpp}` (`// 0x00040e18`, `// 0x00040e38`), `vipclnotifyfilter.{h,cpp}` (`// 0x0004398c`, `// 0x00041c40`, `// 0x00042934`, `// 0x00041990`, `// 0x00041ab8`, `// 0x00041b74`), and stub chain nodes `vaudionotifyfilter`, `vmedianotifyfilter`, `vcameranotifyfilter` (`// TODO(phase3):`).
   - Note on stock hardkey press/release semantics: in `VKeyDriver` (`0xaa77c`), `EV_KEY` `value == 1` is key-down and `value == 0` is key-up; `VControlTask::PostKeyEvent` (`0x322bc`) translates key-up into short-press release (`4`) / long-press release (`6`) on `VIPCLNotifyFilter`! Verify `docs/decompiled/hmi/functions/0x322bc.c` and reproduce that exact state machine (short-press on release `< long_press_ms` dispatches `RADIO` / `APP` immediately on key release, matching `qemu.vip("BTN APP 1"); sleep(0.3); qemu.vip("BTN APP 0")` in `test_hmi_parity.py`!).
2. Implement `vcontroltask.{h,cpp}` (`// 0x0002fd18`, `// 0x00030054`, `// 0x00033c68`, `// 0x0003399c`, `// 0x00030064`, `// 0x000322bc`, `// 0x00036270`), connecting `VControlTask` `IFilterActions` implementation to `VDisplayProxy`, `VAudioProxy`, `VAMFMProxy`, and `VAppConnectProxy`.
3. Add `control_logic/vcontroltask.h` to `hmi_wrap_cpp` and all filter `.cpp` files to `src/hmi/CMakeLists.txt`.

- [ ] **Step 4: Run host tests and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: `test_filters` PASS; `build/hmi/hmi` builds cleanly and passes all gates.

- [ ] **Step 5: Commit**

```bash
git add src/hmi/control_logic/mode/ src/hmi/control_logic/vcontroltask.h \
        src/hmi/control_logic/vcontroltask.cpp src/hmi/tests/test_filters.cpp \
        src/hmi/tests/CMakeLists.txt src/hmi/CMakeLists.txt src/hmi/TRACE.json
git commit -m "hmi: notify filter chain and VControlTask with host unit tests"
```

---

### Task 7: Input Drivers (`VKeyDriver`, `VKnobDriver`, `VMultiTouch`)

**Files:**
- Create:
  - `src/hmi/service/tslib/tslib_api.h`
  - `src/hmi/control_logic/input/input_decoders.h`, `src/hmi/control_logic/input/input_decoders.cpp` (portable, host-testable)
  - `src/hmi/control_logic/input/vkeydriver.{h,cpp}`
  - `src/hmi/control_logic/input/vknobdriver.{h,cpp}`
  - `src/hmi/display/multitouch/vmultitouch.{h,cpp}`
  - `src/hmi/tests/test_input_drivers.cpp`
- Modify: `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `VBaseTask`, `VControlTask::PostKeyEvent`, `hmi_wrapper_igdt_*_permit`, `rcd330_ts` (`libts-1.0.so.0`), `hmi_wrap_cpp`.
- Produces:
  - `src/hmi/service/tslib/tslib_api.h`:
    ```cpp
    // Visteon 5624 libts-1.0.so.0 struct ts_sample: verified 24 bytes from 0x23cac / 0x23dbc.
    struct tsdev;
    struct ts_sample {
        int x;                  // +0x00
        int y;                  // +0x04
        unsigned int pressure;  // +0x08
        int tv_sec;             // +0x0c (ARM32 struct timeval)
        int tv_usec;            // +0x10
        unsigned char reserved; // +0x14
        unsigned char id;       // +0x15
        unsigned char pad[2];   // +0x16..0x17
    };
    extern "C" {
    struct tsdev* ts_open(const char* dev_name, int nonblock);
    int ts_config(struct tsdev* ts);
    int ts_read(struct tsdev* ts, struct ts_sample* samp, int nr);
    int ts_close(struct tsdev* ts);
    }
    ```
  - Portable decoders (`input_decoders.h`):
    - `struct KeyFilterState { uint16_t active_code; bool multi_permit; bool tune_ok_down; };`
    - `bool DecodeHardKeyEvent(KeyFilterState& st, bool hardkey_permit, uint16_t type, uint16_t code, int32_t value, uint8_t* out_code, uint8_t* out_val, bool* out_lock_toggle);` (matches `0xaa77c` `EV_KEY=1`, `code 1..10`, single-key lock/release symmetry)
    - `bool DecodeKnobWord(int32_t word, uint8_t* out_key_code);` (`0x32` -> `7` `TUNE_UP`, `0x33` -> `8` `TUNE_DOWN`, matches `0xaa3e0`)
    - `struct VMULTITOUCH_DATA { int32_t gesture; uint32_t count; int32_t x[3]; int32_t y[3]; int32_t state[3]; };` (matches `0x23d34` & `VDisplayTask::OnTouchEvent` `0x65308`: `state`: `0=idle, 1=press, 2=release, 3=move`)
    - `struct MultiTouchTracker { int32_t x[3], y[3], state[3]; uint32_t count; };`
    - `bool UpdateMultiTouch(MultiTouchTracker& tr, const ts_sample* samples, int n, VMULTITOUCH_DATA* out);` (matches `0x23b14` + `0x23d34`)
  - `class VKeyDriver : public VBaseTask` (`Q_OBJECT`, `0xaa5bc`, `0xaa6bc`, `0xaa6d4` opens `/dev/event0` `O_RDWR`, `0xaa77c` `readKeyDriver`, `static VKeyDriver* Instance()` `0xaa64c`, `bool IsTuneOkDown()` `0xaa70c`).
  - `class VKnobDriver : public VBaseTask` (`Q_OBJECT`, `0xaa0d0`, `0xaa1bc`, `0xaa12c`, `0xaa250` opens `/dev/tuner_knob` `O_RDONLY` + 200 ms `QTimer::singleShot`, `0xaa3e0` `readKnobDriver`).
  - `class VMultiTouch : public VBaseTask` (`Q_OBJECT`, `0x238d4` `Init` using `getenv("TSLIB_TSDEVICE")` with 15× 200 ms retry + `ts_config`, `0x23aec` `Start`, `0x23dbc` `Initialize` `ts_read(..., 3)` loop emitting `SigTouchEvent(VMULTITOUCH_DATA)`).

- [ ] **Step 1: Write failing host test `src/hmi/tests/test_input_drivers.cpp`**

```cpp
#include "control_logic/input/input_decoders.h"
#include "service/tslib/tslib_api.h"
#include "tests/test_runner.h"

#include <string.h>

static void test_ts_sample_size_is_24() {
    // Review Focus 3: Visteon custom libts-1.0.so.0 writes 24-byte samples (0x23cac).
    CHECK_EQ(sizeof(ts_sample), 24u);
}

static void test_hardkey_decoding_and_bounds() {
    KeyFilterState st = {0, true, false};
    uint8_t code = 0, val = 0;
    bool toggle = false;

    // Non-EV_KEY and out-of-range codes (>10 or 0) rejected.
    CHECK(!DecodeHardKeyEvent(st, true, 0, 10, 1, &code, &val, &toggle));
    CHECK(!DecodeHardKeyEvent(st, true, 1, 0, 1, &code, &val, &toggle));
    CHECK(!DecodeHardKeyEvent(st, true, 1, 11, 1, &code, &val, &toggle));

    // APP press locks multi_permit; unrelated key blocked until APP release.
    CHECK(DecodeHardKeyEvent(st, true, 1, 10, 1, &code, &val, &toggle));
    CHECK_EQ(code, 10);
    CHECK_EQ(val, 1);
    CHECK(!st.multi_permit);

    CHECK(!DecodeHardKeyEvent(st, true, 1, 2, 1, &code, &val, &toggle));

    // APP release unlocks multi_permit.
    CHECK(DecodeHardKeyEvent(st, true, 1, 10, 0, &code, &val, &toggle));
    CHECK_EQ(code, 10);
    CHECK_EQ(val, 0);
    CHECK(st.multi_permit);

    // TUNE_OK (code 6) tracks pressed state for VKnobDriver interlock.
    CHECK(DecodeHardKeyEvent(st, true, 1, 6, 1, &code, &val, &toggle));
    CHECK(st.tune_ok_down);
    CHECK(DecodeHardKeyEvent(st, true, 1, 6, 0, &code, &val, &toggle));
    CHECK(!st.tune_ok_down);
}

static void test_knob_word_decoding() {
    uint8_t key = 0;
    CHECK(DecodeKnobWord(0x32, &key));
    CHECK_EQ(key, 7);  // TUNE_UP
    CHECK(DecodeKnobWord(0x33, &key));
    CHECK_EQ(key, 8);  // TUNE_DOWN
    CHECK(!DecodeKnobWord(0x00, &key));
    CHECK(!DecodeKnobWord(0x34, &key));
}

static void test_multitouch_press_move_release_cycle() {
    MultiTouchTracker tr;
    memset(&tr, 0, sizeof(tr));
    VMULTITOUCH_DATA out;

    ts_sample s;
    memset(&s, 0, sizeof(s));
    s.x = 400; s.y = 438; s.pressure = 50;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.count, 1u);
    CHECK_EQ(out.x[0], 400);
    CHECK_EQ(out.y[0], 438);
    CHECK_EQ(out.state[0], 1);  // press

    s.x = 405; s.y = 440; s.pressure = 50;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.state[0], 3);  // move

    s.pressure = 0;
    CHECK(UpdateMultiTouch(tr, &s, 1, &out));
    CHECK_EQ(out.state[0], 2);  // release

    // Repeated zero-pressure idle sample emits nothing.
    CHECK(!UpdateMultiTouch(tr, &s, 1, &out));
}

int main() {
    RUN_TEST(test_ts_sample_size_is_24);
    RUN_TEST(test_hardkey_decoding_and_bounds);
    RUN_TEST(test_knob_word_decoding);
    RUN_TEST(test_multitouch_press_move_release_cycle);
    return test_summary();
}
```

Register `test_input_drivers` in `src/hmi/tests/CMakeLists.txt`.

- [ ] **Step 2: Run host tests to verify failure**

Run: `./tools/build_hmi.sh --host-tests`
Expected: FAIL (`input_decoders.h` / `tslib_api.h` not found).

- [ ] **Step 3: Implement `tslib_api.h`, `input_decoders.{h,cpp}`, `VKeyDriver`, `VKnobDriver`, `VMultiTouch`**

1. Put a compile-time `typedef char ts_sample_must_be_24_bytes[sizeof(ts_sample) == 24 ? 1 : -1];` check in `tslib_api.h` (C++98 static assert; note: on 64-bit host `tv_sec`/`tv_usec` must be `int32_t` rather than `long`, which is also exact 32-bit `long` on ARMv7 Linux!).
2. Implement `input_decoders.cpp` matching `0xaa77c` (`DecodeHardKeyEvent`), `0xaa3e0` (`DecodeKnobWord`), and `0x23b14`/`0x23d34` (`UpdateMultiTouch`).
3. Implement `VKeyDriver` (`// 0x000aa5bc`, `// 0x000aa6bc`, `// 0x000aa6d4`, `// 0x000aa77c`), reading 16-byte Linux `struct input_event` (using `int32_t tv_sec, tv_usec; uint16_t type, code; int32_t value;` matching ARMv7 ABI) from `/dev/event0` with `select()` in its thread loop and calling `VControlTask::Instance()->PostKeyEvent(code, val)`.
4. Implement `VKnobDriver` (`// 0x000aa0d0`, `// 0x000aa1bc`, `// 0x000aa12c`, `// 0x000aa250`, `// 0x000aa3e0`), opening `/dev/tuner_knob`, using `select()` + 4-byte `read`, ignoring reads while `VKeyDriver::Instance()->IsTuneOkDown()` (`0xaa70c`) is true, and calling `VControlTask::Instance()->PostKeyEvent(key, 1)`.
5. Implement `VMultiTouch` (`// 0x000238d4`, `// 0x00023aec`, `// 0x00023dbc`), registering `VMULTITOUCH_DATA` with `QMetaType`, retrying `ts_open` up to 15 times, calling `ts_config`, and reading `ts_read(m_ts, samples, 3)` in its `InitializeSlot()` thread loop, emitting `SigTouchEvent(data)`!
6. Add `vkeydriver.h`, `vknobdriver.h`, `vmultitouch.h` to `hmi_wrap_cpp` and link `rcd330_ts` in `src/hmi/CMakeLists.txt`.

- [ ] **Step 4: Run host tests and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: `test_input_drivers` PASS; `build/hmi/hmi` links `libts-1.0.so.0` cleanly and passes all ABI/DT_NEEDED/Qt/trace checks.

- [ ] **Step 5: Commit**

```bash
git add src/hmi/service/tslib/ src/hmi/control_logic/input/ src/hmi/display/multitouch/ \
        src/hmi/tests/test_input_drivers.cpp src/hmi/tests/CMakeLists.txt \
        src/hmi/CMakeLists.txt src/hmi/TRACE.json
git commit -m "hmi: VKeyDriver, VKnobDriver, and VMultiTouch (24-byte ts_sample) with host tests"
```

---

### Task 8: Core Controls Hierarchy and `ControlCreator`

**Files:**
- Create:
  - `src/hmi/display/controls/controlbase/vvisibilitybase.{h,cpp}`
  - `src/hmi/display/controls/controlbase/vmoveablebase.{h,cpp}`
  - `src/hmi/display/controls/controlbase/vdynamicbase.{h,cpp}`
  - `src/hmi/display/controls/controlbase/itembase.{h,cpp}`
  - `src/hmi/display/controls/controlbase/complexcontrol.{h,cpp}`
  - `src/hmi/display/controls/controlbase/buttonbase.{h,cpp}`
  - `src/hmi/display/controls/controlbase/igdtbuttonbase.{h,cpp}`
  - `src/hmi/display/controls/controlparts/igdttextitem.{h,cpp}`
  - `src/hmi/display/controls/controlparts/compositebackground.{h,cpp}`
  - `src/hmi/display/controls/controlparts/compositeitem.{h,cpp}`
  - `src/hmi/display/controls/controlparts/igdtbutton.{h,cpp}`
  - `src/hmi/display/controls/controlparts/vstatusline.{h,cpp}`
  - `src/hmi/display/base/controlcreator.{h,cpp}`
  - `src/hmi/display/base/control_layout.{h,cpp}` (portable, host-testable 1/3/9-slice sizing + node lookup + button state machine)
  - `src/hmi/tests/test_controls.cpp`
- Modify: `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IgdtSkinTables`, `IgdtRuntime`, `IgdtTouchEvent`, `igdt_wrapper.h`, `hmi_wrap_cpp`.
- Produces:
  - Portable `control_layout.h`:
    - `const IgdtNode* IgdtFindNodeByTag(const IgdtSkinTables& t, uint8_t screen_id, uint32_t tag);` (`// 0x002d6884 ControlCreator::GetElementRoot`)
    - `bool IgdtCalcNodeRect(const IgdtSkinTables& t, const IgdtNode* node, int* x, int* y, int* w, int* h);` (`// 0x002d690c ControlCreator::GetElementRect`)
    - `void IgdtApplyCompositeSize(const IgdtSkinTables& t, IgdtRuntime& rt, const IgdtNode* root, int x, int y, int w, int h, int slice_mode);` (`slice_mode`: `1` = single/horizontal scale, `3` = 3-slice horizontal `[left, mid_stretched, right]`, `9` = 9-slice grid; updates `dyn_grp` / child `dyn_img` positions and scale matrices matching `0x2c82c4` `CompositeBackground::SetSize`!)
    - `enum ButtonState { BTN_NORMAL = 0, BTN_PRESSED = 1, BTN_DISABLED = 2, BTN_SELECTED = 3 };`
    - `struct ButtonClickResult { bool state_changed; int new_state; bool clicked; };`
    - `ButtonClickResult EvalButtonTouch(int cur_state, uint32_t event_type);` (press `1` -> `BTN_PRESSED`; drag-out cancel `0x80000000` -> `BTN_NORMAL`; release `3` without `0x80000000`/`0x40000000` -> `BTN_NORMAL` + `clicked = true`; matches `0x2ea27c`!)
  - Stock control classes:
    - `VVisibilityBase` (`0x2f99bc`), `VMoveableBase` (`0x2f867c..0x2f8910`), `VDynamicBase` (`0x2f810c..0x2f83d4`).
    - `Item` / `ItemBase` (`Q_OBJECT`, `0x2d12e8`), `ComplexControl` (`0x2c7efc`, `0x2c80d0` `AddChild`, `0x2c7714` `SetSize`).
    - `ButtonBase` (`Q_OBJECT`, `0x2c6fdc` `SetState`, `0x2c7074`, `0x2c748c` `OnHold`, signal `SigClicked(Item*, int32_t, int32_t, int32_t)`).
    - `IgdtButtonBase` (`0x2e9fd4` `GetTouchId`, `0x2ea060` `SetTouchArea`, `0x2ea1f0` `DisableDbClick`, `0x2ea27c` `TouchEventHandler`).
    - `IgdtTextItem` (`0x2d02ac` ctor taking `uint32_t text_layout_id`, `0x2cf428` `SetText(uint32_t str_id)`, `SetText(const char* utf8)`, `0x2d0414` `SetGlowIds`).
    - `CompositeBackground` (`0x2c82c4` `SetSize`), `CompositeItem` (`0x2c98f8`).
    - `IgdtButton` (`0x2cd50c` `SetState` — sets background branch/state via `VDynamicBase`/`hmi_wrapper_igdt_set_branch`, e.g. switching `VBtnInfo` branch `0x01000045` to `0` on initial `SetState(BTN_NORMAL)` and `1` on `BTN_PRESSED`!, `0x2cd8fc` `SetSelected`, `0x2cda98` `SetBgStatus`, `SetText(int idx, uint32_t str_id)`).
    - `VStatusLine` (`0x2e61f0..0x2e61f8` — updates `StatusBar` screen `0xfe` clock text, exterior temperature text/unit from `VBAPProxy`, mute/BT icons).
    - `ControlCreator`: `GetElementRoot` (`0x2d6884`), `GetElementRect` (`0x2d690c`), `CreateComposite` (`0x2d81f4`/`0x2dbd04`), `CreateButton` (`0x2d8f10`/`0x2dd4e0` — discovers background branch/state, touch group tag, sets touch area `(x, y, w, h)`, attaches text items, initializes `SetState(BTN_NORMAL)`!).

- [ ] **Step 1: Write failing host test `src/hmi/tests/test_controls.cpp`**

```cpp
#include "display/base/control_layout.h"
#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_touch.h"
#include "tests/test_runner.h"

#ifdef HMI_HAVE_IGDT_TABLES
static void test_control_creator_finds_appnoconn_and_info_nodes() {
    // vAppNoConnection (screen 0x04) control bars and VBtnInfo.
    const IgdtNode* bar1 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100164u);
    const IgdtNode* bar2 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100165u);
    const IgdtNode* btn_info = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100166u);
    CHECK(bar1 != 0);
    CHECK(bar2 != 0);
    CHECK(btn_info != 0);

    // vAppConnectInfo (screen 0x03) divider and Back button.
    const IgdtNode* divider = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x40100163u);
    const IgdtNode* btn_back = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x4010015du);
    CHECK(divider != 0);
    CHECK(btn_back != 0);
    CHECK(IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0xdeadbeefu) == 0);
    CHECK(IgdtFindNodeByTag(g_igdtTablesVw, 0xff, 0x40100166u) == 0);
}

static void test_3slice_composite_size_updates_runtime() {
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    const IgdtNode* bar1 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100164u);
    CHECK(bar1 != 0);
    IgdtApplyCompositeSize(g_igdtTablesVw, rt, bar1, 0, 0x182, 800, 0x5e, 3);
    int16_t gx = 0, gy = 0;
    CHECK_EQ(IgdtGetPosXY(rt, 0x40100164u, &gx, &gy), IGDT_OK);
    CHECK_EQ(gx, 0);
    CHECK_EQ(gy, 0x182);
}
#endif

static void test_button_touch_state_machine() {
    // Normal press -> Pressed, no click yet.
    ButtonClickResult r = EvalButtonTouch(BTN_NORMAL, IGDT_TOUCH_PRESS);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_PRESSED);
    CHECK(!r.clicked);

    // Release inside -> Normal + clicked.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_RELEASE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(r.clicked);

    // Release with drag-out cancel (0x80000003) -> Normal, NOT clicked.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_OUTSIDE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(!r.clicked);

    // Release with page-change cancel (0x40000003) -> Normal, NOT clicked.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_PAGE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(!r.clicked);
}

int main() {
#ifdef HMI_HAVE_IGDT_TABLES
    RUN_TEST(test_control_creator_finds_appnoconn_and_info_nodes);
    RUN_TEST(test_3slice_composite_size_updates_runtime);
#endif
    RUN_TEST(test_button_touch_state_machine);
    return test_summary();
}
```

Register `test_controls` in `src/hmi/tests/CMakeLists.txt`.

- [ ] **Step 2: Run host tests to verify failure**

Run: `./tools/build_hmi.sh --host-tests`
Expected: FAIL (`control_layout.h` not found).

- [ ] **Step 3: Implement `control_layout.{h,cpp}`, `controlbase/*`, `controlparts/*`, and `controlcreator.{h,cpp}`**

1. Before implementing `IgdtApplyCompositeSize` (`0x2c82c4` `CompositeBackground::SetSize`) and `CreateButton` (`0x2dd4e0`), read `docs/decompiled/hmi/functions/0x2c82c4.c` and `0x2dd4e0.c` completely to verify the exact child ordering (left/mid/right 3-slice child offsets and scale factors, and how `CreateButton` identifies the state-branch node such as `0x01000045` on `VBtnInfo` and `m_btn_back` on `vAppConnectInfo`!).
2. Implement `control_layout.cpp` (`// 0x002d6884`, `// 0x002d690c`, `// 0x002c82c4` helper, `EvalButtonTouch`).
3. Implement `controlbase/{vvisibilitybase,vmoveablebase,vdynamicbase,itembase,complexcontrol,buttonbase,igdtbuttonbase}.{h,cpp}` and `controlparts/{igdttextitem,compositebackground,compositeitem,igdtbutton,vstatusline}.{h,cpp}` and `base/controlcreator.{h,cpp}`.
   - Pay special attention to `IgdtButton::SetState` (`// 0x002cd50c`): when `CreateButton` finishes constructing `VBtnInfo` (`0x40100166`), it initializes state to `BTN_NORMAL` (`0`), which sets branch `0x01000045` to child `0` (clearing the stock `.rodata` initial glow!) and switches it to `1` while pressed.
   - Pay special attention to `VStatusLine` (`// 0x002e61f8`): read `0x2e61f8.c` and `0x2ad60.c` to populate the exact `StatusBar` (`0xfe`) text IDs (clock `HH:MM`, exterior temp string + unit) and status icon branch/state IDs!
4. Add `itembase.h` and `buttonbase.h` (and any `Q_OBJECT` control headers) to `hmi_wrap_cpp` and all `.cpp` files to `src/hmi/CMakeLists.txt`.

- [ ] **Step 4: Run host tests and cross-build + ABI gate**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
```
Expected: `test_controls` PASS; `build/hmi/hmi` builds and passes all ABI/Qt/trace checks.

- [ ] **Step 5: Commit**

```bash
git add src/hmi/display/controls/ src/hmi/display/base/ \
        src/hmi/tests/test_controls.cpp src/hmi/tests/CMakeLists.txt \
        src/hmi/CMakeLists.txt src/hmi/TRACE.json
git commit -m "hmi: core display control hierarchy, VStatusLine, and ControlCreator with host tests"
```

---

### Task 9: `VBasePage`, `VPageManager`, `VDisplayTask`, Interactive Pages (`vAppNoConnection`, `vAppConnectInfo`, `VTunerMainPage` Shell), and Full `main.cpp`

**Files:**
- Create:
  - `src/hmi/display/page/base/basepage.{h,cpp}`
  - `src/hmi/display/page/base/pagemanager.{h,cpp}`
  - `src/hmi/display/vdisplaytask.{h,cpp}`
  - `src/hmi/display/page/appconnect/vappnoconnection.{h,cpp}`
  - `src/hmi/display/page/appconnect/vappconnectinfo.{h,cpp}`
  - `src/hmi/display/page/tuner/vtunermainpage.{h,cpp}`
- Modify: `src/hmi/main.cpp`, `src/hmi/CMakeLists.txt`

**Interfaces:**
- Consumes: `VBaseTask`, `VmfThread`, all proxies (Task 5), `VControlTask` (Task 6), `VKeyDriver`/`VKnobDriver`/`VMultiTouch` (Task 7), `ControlCreator` + controls + `VStatusLine` (Task 8), `igdt_wrapper` + `VReflash` (Task 3).
- Produces:
  - `class VBasePage : public ComplexControl` (`Q_OBJECT`, `0x2a7d0` ctor with `VStatusLine* m_statusline`, `0x2a8fc` dtor, `virtual void Start()` `0x2adf0`, `virtual void Stop()` `0x2aaa8`, `virtual int GetStatusLineScreenId() const` returning `0xfe` by default and `-1` on pages without status line, `void UpdateHeadlineDisplay()` `0x2ad60`, `bool DispatchIgdtTouch(uint32_t tag, const IgdtTouchEvent& ev)` routing to matching child `IgdtButtonBase`).
  - `class VPageManager : public QObject` (`Q_OBJECT`, `0x2b2a4` ctor with 29-slot layer table `{VBasePage* page; uint8_t scr_id; bool active;}`, `static VPageManager* Instance()` `0x2b398`, `void OnShowPage(uint8_t scr_id)` `0x2c34c` logging `" ==== HMI ==== INFO: ##########################VPageManager::ShowPage %d"`, `void InsertPage(uint8_t scr_id, int mode, int layer, VBasePage* page)` `0x2c1b4`, `void ClosePage(uint8_t scr_id)`, `VBasePage* FindPage(uint8_t scr_id)` `0x2b8b8`, `uint8_t GetHighestPriPageID()` `0x2bb60`, `void OnTouchEvent(int32_t x, int32_t y, int raw_type)` `0x2ba34`, `void OnIgdtTouchEvent(uint32_t tag, const IgdtTouchEvent& ev)`).
  - `class VDisplayTask : public VBaseTask` (`Q_OBJECT`, `0x63344` ctor owning `VMultiTouch` + `VReflash`, `static VDisplayTask* Instance()` `0x633a4`, `void Init()` `0x65238`, `void Run()` `0x63584`, `void InitializeSlot()` `0x633b4`, `void OnDisplayTimeOut()` `0x649d4`, `void OnShowPage(unsigned char)`, `void OnClosePage(unsigned char)`, `void OnTouchEvent(VMULTITOUCH_DATA)` `0x65308`, `void OnIgdtTouchEvent(unsigned int, GFX_MGR02_TOUCH_QDATA)` `0x63298` target, `void SlotKeyEventNotify(unsigned char, unsigned char)` `0x657dc`).
  - `class vAppNoConnection : public VBasePage` (`Q_OBJECT`, `eScrId 0x04`, `0x33a660` ctor, `0x33aa60` `InitScreen`, `0x33ae48` `Start`, `0x33aa40` `Stop`, `0x33ae70` `OnBtnInfoClicked` -> `VDisplayProxy::Instance()->PushPage(3)`).
  - `class vAppConnectInfo : public VBasePage` (`Q_OBJECT`, `eScrId 0x03`, `0x345a60` ctor, `0x3462c8` `InitScreen`, `Start`, `0x3462a4` `Stop`, `GetStatusLineScreenId() { return -1; }`, `OnBtnBackClicked` -> `VDisplayProxy::Instance()->ClosePage(3)`).
  - `class VTunerMainPage : public VBasePage` (`Q_OBJECT`, `eScrId 0xf9`, Phase-2 boot shell: shows `0xf9` + `StatusBar` `0xfe` on `Start()`, removes on `Stop()`, `// TODO(phase3): full tuner page`).
  - `main.cpp`: `logo_black_avoid_handle()` (`// 0x00023514`), stock startup sequence (`VControlTask`, `VDisplayTask`, `VKeyDriver`, `VKnobDriver`, `VmfThread::start()`, `VPowerProxy::SendWakeStateToPowerAgent(1)`, boot queries `VAMFMProxy`, `VMediaProxy`, `VBAPProxy`, `VAudioProxy`, `VAppConnectProxy`), and removal of `HMI_DEV_SCREEN`.

- [ ] **Step 1: Verify boot-screen & page expectations in `tools/tests/test_hmi_boot.py`**

Extend `tools/tests/test_hmi_boot.py::HmiBootTest::test_recreated_hmi_boots_and_stays_up` to assert that the recreated HMI not only prints `[HMI_START]HMI main start` once without segfaults, but also completes VMF handshake and renders the initial screen (`VPageManager::ShowPage 249` AND `gl_fb_shim: eglSwapBuffers #1`) just like stock!

```python
    def test_recreated_hmi_boots_and_stays_up(self):
        hmi = ROOT / "build/hmi/hmi"
        self.assertTrue(hmi.is_file(), f"{hmi} missing; run tools/build_hmi.sh")
        with Rcd330Qemu(hmi=str(hmi)) as qemu:
            qemu.wait_serial(BANNER, BANNER_TIMEOUT_S)
            qemu.wait_serial(r"VPageManager::ShowPage 249[\s\S]*gl_fb_shim: eglSwapBuffers #1", 120)
            time.sleep(STAY_UP_S)
            text = qemu.serial_text()
        self.assertEqual(text.count("[HMI_START]HMI main start"), 1,
                         "hmi was (re)started more than once; serial tail:\n" + text[-3000:])
        self.assertNotIn("Segmentation fault", text)
```

- [ ] **Step 2: Implement `VBasePage`, `VPageManager`, `VDisplayTask`, the three pages, and full `main.cpp`**

1. `display/page/base/basepage.{h,cpp}`: implement `VBasePage` (`0x2a7d0`, `0x2a8fc`, `0x2ad60`, `0x2adf0`, `0x2aaa8`). Ensure `VBasePage::DispatchIgdtTouch` walks registered child `IgdtButtonBase` widgets and forwards `(x, y, type)` to the child whose `GetTouchId() == tag`.
2. `display/page/base/pagemanager.{h,cpp}`: implement `VPageManager` (`0x2b2a4`, `0x2b398`, `0x2b8b8`, `0x2ba34`, `0x2bb60`, `0x2c1b4`, `0x2c34c`):
   - `OnShowPage(scr_id)` logs exact stock format `" ==== HMI ==== INFO: ##########################VPageManager::ShowPage %d"`, instantiates `vAppConnectInfo` (`case 3`), `vAppNoConnection` (`case 4`), `VTunerMainPage` (`case 249`), or logs `"Phase-3 page %d not yet implemented"` and returns cleanly without touching active layers for unimplemented Phase-3 IDs (Review Focus 5).
   - When switching main pages (`0xf9` <-> `0x04`), stops and removes the replaced lower-layer main page cleanly; when pushing overlay page `0x03` (`layer 2`) over `0x04` (`layer 1`), pauses `0x04`'s status line (`0xfe`) and restores `0x04` + `0xfe` when `ClosePage(3)` runs!
3. `display/vdisplaytask.{h,cpp}`: implement `VDisplayTask` (`0x63344`, `0x633a4`, `0x65238`, `0x63584`, `0x633b4`, `0x649d4`, `0x65308`, `0x657dc`), register `hmi_wrapper_igdt_set_touch_callback` to emit `SigIgdtTouchEvent(tag, qdata)` across threads onto `VDisplayTask`'s Qt event loop, and bind `VDisplayProxy::Instance()->SetDisplaySink(this)`.
4. `display/page/appconnect/vappnoconnection.{h,cpp}`: exact port of `0x33a660` (brand tint on `0x42100284..0x42100286`, `ControlCreator::CreateComposite` on `0x40100164` & `0x40100165`, `IgdtTextItem(0x1a)` & `(0x19)`, `ControlCreator::CreateButton` on `0x40100166` with `0x1c` text `0x1c8d7` `"Info"` connected to `OnBtnInfoClicked()`), `0x33aa60` (`InitScreen`: Y pos `0x71` on `0x45100058`, strings `0x1c8fb`/`0x1c992`, branch `0x01000043` & states `0x030000ba..0x030000be` from `VAppConnectProxy`), `0x33ae48` (`Start`), `0x33aa40` (`Stop`), `0x33ae70` (`OnBtnInfoClicked` -> `PushPage(3)`).
5. `display/page/appconnect/vappconnectinfo.{h,cpp}`: exact port of `0x345a60` (brand tint `0x42100268..0x4210026a`, divider `0x40100163`, headline `0x0f` + lines `0x10..0x18`, Back button `0x4010015d` at `(719, 1, 61, 61)` connected to `OnBtnBackClicked()`), `0x3462c8` (`InitScreen`), `Start` (inserts screen `3`), `0x3462a4` (`Stop` removes screen `3`), `OnBtnBackClicked` -> `ClosePage(3)`.
6. `display/page/tuner/vtunermainpage.{h,cpp}`: Phase-2 shell for `eScrId 0xf9` (`Start` calls `VBasePage::Start()` + `hmi_wrapper_igdt_insert_page(0xf9, 0, this)`; `Stop` calls `VBasePage::Stop()` + `hmi_wrapper_igdt_remove_page(0xf9, 0)`).
7. `src/hmi/main.cpp`: implement `logo_black_avoid_handle()` (`// 0x00023514` using `<linux/fb.h>` `FBIOGET_VSCREENINFO`/`FBIOPUT_VSCREENINFO` clearing bit 7 of `activate`), wire all Phase-2 tasks & startup proxy calls in exact `0x10e58` order, keep `// TODO(phase3):` comments on `VCameraTask` (`0xde600`) and `VMLC*Task` (`0xe08f4..0xeba70`), and delete `DevDrawScreen()` / `DevFillAppNoConnText()`.
8. Add all new headers to `hmi_wrap_cpp` and `.cpp` files to `src/hmi/CMakeLists.txt`.

- [ ] **Step 3: Build, run ABI gate, and run QEMU boot test**

Run:
```bash
python3 src/hmi/tools/gen_trace.py
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v
RCD330_QEMU_TESTS=1 python3 -m unittest tools/tests/test_hmi_boot.py -v
```
Expected: All build/ABI/trace checks PASS; `test_recreated_hmi_boots_and_stays_up` logs `VPageManager::ShowPage 249` + `gl_fb_shim: eglSwapBuffers #1`, stays up 60 s with banner count == 1 and zero segfaults!

- [ ] **Step 4: Commit**

```bash
git add src/hmi/display/page/ src/hmi/display/vdisplaytask.h src/hmi/display/vdisplaytask.cpp \
        src/hmi/main.cpp src/hmi/CMakeLists.txt src/hmi/TRACE.json tools/tests/test_hmi_boot.py
git commit -m "hmi: VBasePage, VPageManager, VDisplayTask, interactive App-Connect pages, and full main startup"
```

---

### Task 10: Remove Phase-1 Parity Masks, End-to-End Interactive QEMU Suite, Traceability & Docs Sync

**Files:**
- Modify: `tools/run_rcd330.sh`, `tools/hmi_qemu.py`, `tools/tests/test_hmi_parity.py`, `src/hmi/README.md`, `docs/hmi-internals.md`, `src/hmi/TRACE.json`
- Create: `tools/tests/test_hmi_interactive.py`

**Interfaces:**
- Consumes: Complete Phase-2 `build/hmi/hmi` (Task 9), QEMU `-M rcd330` `atmel_mxt` (`qemu/hw/input/atmel_mxt.c`) + `rcd330_vip` (`qemu/hw/misc/rcd330_vip.c`).
- Produces:
  - `tools/run_rcd330.sh --qmp <unix-socket>` adding `-qmp unix:<sock>,server,nowait`.
  - `tools/hmi_qemu.py`:
    - `Rcd330Qemu.touch(x: int, y: int, hold_s: float = 0.15) -> None` — sends QMP `input-send-event` absolute coordinates `(int(x * 32767 / 799), int(y * 32767 / 479))` + `btn left down=True`, sleeps `hold_s`, then `btn left down=False`.
    - `Rcd330Qemu.knob(steps: int) -> None` — sends `self.vip(f"ENC TUNE {steps}")`.
  - Mask-free `tools/tests/test_hmi_parity.py` (no `HMI_DEV_SCREEN`, no `dynamic_image_boxes`, no `button_state_boxes`; syncs clock via `VIP TIME 2026 11 24 12 00 00` so `StatusBar` `0xfe` matches too; compares both `AppNoConn` `0x04` and `AppConnectInfo` `0x03` at ≥ 99.5 % within ±4 per channel!).
  - `tools/tests/test_hmi_interactive.py` exercising full boot + hardkeys (`APP`, `RADIO`, Phase-3 `MEDIA`/`SETUP` safety) + touchscreen (`VBtnInfo` -> `AppConnectInfo` -> `Back` -> `AppNoConn`) + rotary tune knob (`ENC TUNE +1/-1`).

- [ ] **Step 1: Add `--qmp` to `tools/run_rcd330.sh` and `touch()`/`knob()` to `tools/hmi_qemu.py`**

1. In `tools/run_rcd330.sh`, parse `--qmp <unix-socket>` / `--qmp=<unix-socket>` alongside `--monitor` and append `-qmp "unix:${QMP_SOCK},server,nowait"` to `QEMU_CMD`.
2. In `tools/hmi_qemu.py`, create `self.qmp_path = os.path.join(self._tmpdir, "qmp.sock")`, pass `--qmp self.qmp_path`, and add:

```python
    def _qmp_cmd(self, payload, timeout=10.0):
        import json
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(timeout)
            s.connect(self.qmp_path)
            f = s.makefile("rwb", buffering=0)
            f.readline()  # QMP greeting
            f.write(b'{"execute":"qmp_capabilities"}\n')
            f.readline()  # capabilities return
            f.write((json.dumps(payload) + "\n").encode("ascii"))
            reply = json.loads(f.readline().decode("utf-8"))
            if "error" in reply:
                raise RuntimeError(f"QMP error for {payload}: {reply}")
            return reply

    def touch(self, x, y, hold_s=0.15):
        """Tap screen coordinate (x, y) in 800x480 via QMP -> atmel_mxt -> tslib."""
        ax = max(0, min(32767, int(round(x * 32767 / 799.0))))
        ay = max(0, min(32767, int(round(y * 32767 / 479.0))))
        self._qmp_cmd({
            "execute": "input-send-event",
            "arguments": {"events": [
                {"type": "abs", "data": {"axis": "x", "value": ax}},
                {"type": "abs", "data": {"axis": "y", "value": ay}},
                {"type": "btn", "data": {"down": True, "button": "left"}},
            ]},
        })
        time.sleep(hold_s)
        self._qmp_cmd({
            "execute": "input-send-event",
            "arguments": {"events": [
                {"type": "btn", "data": {"down": False, "button": "left"}},
            ]},
        })

    def knob(self, steps):
        """Rotate the physical TUNE rotary encoder (+CW / -CCW)."""
        self.vip(f"ENC TUNE {int(steps)}")
```

- [ ] **Step 2: Update `tools/tests/test_hmi_parity.py` to remove Phase-1 masks & `HMI_DEV_SCREEN` and test `AppNoConn` (`0x04`) + `AppConnectInfo` (`0x03`)**

1. Remove `HMI_DEV_SCREEN` from `HmiParityTest.setUpClass`: boot our recreated `build/hmi/hmi` with the exact same sequence as stock:
   - Wait for `BANNER`, then `VPageManager::ShowPage 249[\s\S]*gl_fb_shim: eglSwapBuffers #1`.
   - Send `qemu.vip("TIME 2026 11 24 12 00 00")` (syncing wall clock on both stock and recreated runs).
   - Send `BTN APP 1`, sleep 0.3 s, `BTN APP 0`; wait for `VPageManager::ShowPage 4\b` + settle + `screendump_stable(appnoconn_ppm)`.
   - Tap `VBtnInfo`: `qemu.touch(400, 438)`; wait for `VPageManager::ShowPage 3\b` + settle + `screendump_stable(appinfo_ppm)`.
2. **Delete `dynamic_image_boxes()` and `button_state_boxes()`**! Compare the **entire 800×480 frame** (`boxes = []`) for both `test_appnoconn_full_unmasked` and `test_appconnectinfo_full_unmasked` at `>= 99.5 %` within `±4` per channel (retaining the all-black negative control so the check can never become vacuous!). If live `TIME` propagation has a 1-minute drift between boots, assert at least that only the clock digit sub-box `(350, 0, 450, 40)` differs — never re-mask dynamic images or button glow!

- [ ] **Step 3: Create `tools/tests/test_hmi_interactive.py`**

```python
#!/usr/bin/env python3
"""End-to-end interactive QEMU test for Phase 2 recreated opt/hmi.

Drives boot -> BTN APP -> touch VBtnInfo -> touch Back -> BTN RADIO ->
ENC TUNE +1/-1 -> unimplemented Phase-3 keys (BTN MEDIA, BTN SETUP) ->
BTN APP again, asserting exact page transitions, knob delivery, zero
segfaults, and single HMI_START banner.

Skipped unless RCD330_QEMU_TESTS=1.
"""
import os
import pathlib
import sys
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from hmi_qemu import Rcd330Qemu  # noqa: E402

HMI = ROOT / "build/hmi/hmi"


@unittest.skipUnless(os.environ.get("RCD330_QEMU_TESTS") == "1", "set RCD330_QEMU_TESTS=1")
class HmiInteractiveTest(unittest.TestCase):
    def test_boot_hardkeys_touch_knob_and_phase3_safety(self):
        self.assertTrue(HMI.is_file(), f"{HMI} missing; run tools/build_hmi.sh")
        with Rcd330Qemu(hmi=str(HMI)) as qemu:
            qemu.wait_serial(r"\[HMI_START\]HMI main start", 240)
            qemu.wait_serial(r"VPageManager::ShowPage 249[\s\S]*gl_fb_shim: eglSwapBuffers #1", 120)

            # Hardkey APP -> AppNoConn (4)
            qemu.vip("BTN APP 1")
            time.sleep(0.3)
            qemu.vip("BTN APP 0")
            qemu.wait_serial(r"VPageManager::ShowPage 4\b", 30)

            # Touch VBtnInfo (400, 438) -> AppConnectInfo (3)
            time.sleep(1.0)
            qemu.touch(400, 438)
            qemu.wait_serial(r"VPageManager::ShowPage 3\b", 30)

            # Touch Back button (749, 31) -> back to AppNoConn (4)
            time.sleep(1.0)
            qemu.touch(749, 31)
            qemu.wait_serial(r"VPageManager::ShowPage 3\b[\s\S]*VPageManager::ShowPage 4\b", 30)

            # Hardkey RADIO -> TunerMain boot shell (249)
            qemu.vip("BTN RADIO 1")
            time.sleep(0.3)
            qemu.vip("BTN RADIO 0")
            qemu.wait_serial(r"VPageManager::ShowPage 4\b[\s\S]*VPageManager::ShowPage 249\b", 30)

            # Tune rotary encoder (+1 CW, -1 CCW) -> reaches VIPCLNotifyFilter
            qemu.knob(1)
            time.sleep(0.3)
            qemu.knob(-1)
            qemu.wait_serial(r"HandleLPCLHardKeyTuneUp", 20)
            qemu.wait_serial(r"HandleLPCLHardKeyTuneDown", 20)

            # Review Focus 5: Phase-3 hardkeys do not crash or lock out later APP press.
            for btn in ("MEDIA", "SETUP"):
                qemu.vip(f"BTN {btn} 1")
                time.sleep(0.3)
                qemu.vip(f"BTN {btn} 0")
                time.sleep(0.5)
            qemu.vip("BTN APP 1")
            time.sleep(0.3)
            qemu.vip("BTN APP 0")
            qemu.wait_serial(r"HandleLPCLHardKeyTuneDown[\s\S]*VPageManager::ShowPage 4\b", 30)

            text = qemu.serial_text()
        self.assertEqual(text.count("[HMI_START]HMI main start"), 1)
        self.assertNotIn("Segmentation fault", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Run full verification suites and sync docs & `TRACE.json`**

1. Regenerate `src/hmi/TRACE.json`: `python3 src/hmi/tools/gen_trace.py`.
2. Update `src/hmi/README.md` (Phase 2 status, host `moc` note, updated directory table, mask-free parity status) and `docs/hmi-internals.md` (newly verified Phase 2 facts: 24-byte Visteon `struct ts_sample`, 29-layer IGDT table `0xe5128c`/`0x7948c8`, `VReflash` thread `0x62e60`, `ControlCreator` 3-slice layout). Keep all doc links repo-relative (no `file://` or machine-specific paths).
3. Run every verification gate:
```bash
./tools/build_hmi.sh
./tools/build_hmi.sh --host-tests
python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v
RCD330_QEMU_TESTS=1 python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v
```
Expected: Every host test, Python ABI/table/trace test, QEMU boot test, mask-free `AppNoConn`/`AppConnectInfo` pixel-parity test, and interactive hardkey/touch/knob test PASSES.

- [ ] **Step 5: Commit**

```bash
git add tools/run_rcd330.sh tools/hmi_qemu.py tools/tests/test_hmi_parity.py \
        tools/tests/test_hmi_interactive.py src/hmi/README.md docs/hmi-internals.md src/hmi/TRACE.json
git commit -m "hmi: remove Phase-1 parity masks, add QMP touch & interactive QEMU suite, sync Phase-2 docs"
```

---

## Not in this plan (next plans)

- **Phase 3 (Pages by area, one plan/increment per area with full proxy logic):**
  - Tuner (`VTunerMainPage` full AM/FM/RDS/presets/station list/setup) -> Media -> Phone/BT -> Sound -> System Setup (incl. language) -> Car (BAP/MFA/OPS/Climate) -> Popups (85 screens) -> Service/ETM/EOL -> App-Connect projection (CarPlay/AA/CarLife/MirrorLink + `VMLC*Task`) -> Reverse Camera (`VCameraTask` + V4L2 overlay).
- **Phase 4:** Skoda asset/color overlay on 5624 layouts + extensible language table (`de_DE` + outward-code safety audit).
- **Phase 5:** Device hardening (real Vivante/tslib validation, watchdog/recovery verification, first on-car flash).
