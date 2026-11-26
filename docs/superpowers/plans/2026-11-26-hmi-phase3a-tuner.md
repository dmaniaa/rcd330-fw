# HMI Recreation — Phase 3a (Tuner / AM-FM Area) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the complete stock AM/FM tuner area of `opt/hmi` 5624 work in the recreated HMI: full `VAMFMProxy`, the tuner data store, the DISPLAY_MSG path from proxies to pages, the tuner controls, and all seven tuner pages (`0xf9` main, `0xfa` setup, `0xfb` station list, `0xfc` station update, `0xfd` store preset, `0xf8` delete preset, `0xdd` auto-store). Each page must match stock pixel for pixel in QEMU and send the same tuner command sequence.

**Architecture:** This is the structural port from spec §4 (approach A), continued from Phase 2. Wire formats, PS-name normalisation, tuner data, list/preset maths and frequency formatting go into portable C++98 units that run under host `ctest`. The Qt-side proxy, pages and controls are thin glue around them. The QEMU VIP tuner peer, `HOSTCMD`/`TUNER_TRACE` commands, and `Rcd330Qemu` trace/drag helpers are provided by Emulator Plan 3 (`docs/superpowers/plans/2026-11-25-emu-plan3-phase3-vip-peripherals-bt-rvc-usb.md`, Task 5), so stock and recreated HMIs can be driven through the same scenario and compared on both frames and the tuner command trace.

**Tech Stack:** C++98 (`-std=gnu++98`, GCC 4.6 libstdc++ headers) through `zig` 0.17.1 for `armv7a-linux-gnueabi`, CMake + Ninja, host Qt 4.7.4 `moc`, Qt Embedded 4.7.4 `libQtCore.so.4`, `libvmf_client.so`, Python 3 `unittest`, QEMU `-M rcd330` (VIP TCP chardev + QMP).

**Spec:** `docs/superpowers/specs/2026-11-24-hmi-recreation-design.md` (§5.6, §5.8, §5.9 phase 3)

**Prerequisites:**
- **Required:** `docs/superpowers/plans/2026-11-25-emu-plan3-phase3-vip-peripherals-bt-rvc-usb.md` Task 5 (VIP AM/FM tuner peer in `qemu/hw/misc/rcd330_vip.c`, `0x3f` preset block fix, `HOSTCMD`/`TUNER_TRACE`, `Rcd330Qemu.hostcmd`/`tuner_trace`/`tuner_events`/`drag`, and `tools/tests/test_vip_tuner.py`).
- **Recommended:** `docs/superpowers/plans/2026-11-25-emu-plan1-vip-pss-determinism-and-shim-fixes.md` (eliminates the `opt/pss` warm-RAM boot stall/language race and adds headless `--coding-file` qdev properties for MQB).

## Phase 3 Roadmap (scope check)

Spec §5.9 phase 3 covers ten areas, and each one is a separate subsystem with its own proxy, pages and controls. **Phase 3 is therefore split into one plan per area.** Each plan is written when the previous one has merged, so it can build on what that area taught us (shared controls, DISPLAY_MSG routing, scenario harness). Every plan ends with QEMU parity for its area.

| Plan | Area | Proxy / main sources |
|---|---|---|
| **3a (this plan)** | Tuner (AM/FM) | `VAMFMProxy`, `display/page/tuner/*` |
| 3b | Media (USB/SD/AUX/BT audio) | `VMediaProxy`, `display/page/media/*` |
| 3c | Phone / BT | `VBTProxy`, `display/page/phone*/*` |
| 3d | Sound | `VAudioProxy` (rest), `display/page/sound/*` (incl. `0xfe` `VSoundVolumeSetup`) |
| 3e | System setup (incl. language, clock) | `VSystemTimeCalendarProxy`, `display/page/system_setup/*`, `system_time_calendar/*` |
| 3f | Car (BAP) | `VBAPProxy` (rest), `display/page/car/*` |
| 3g | Popups (85) | `display/page/popup/*` |
| 3h | Service / ETM / EOL | `VServiceProxy`, `VDiagProxy`, `display/page/{service,etm,eol_test}/*` |
| 3i | App-Connect / CarPlay / AA / CarLife / MirrorLink | five proxies, `display/page/{appconnect,carplay,carlife,mirrorlink}/*`, `mlc_cb_ctrl/*` |
| 3j | Camera | `camera_detect/vcameratask.cpp`, RVC screen `0xdf` |

Buttons in the tuner area that open pages from a later area are routed to `VPageManager::ShowPage`. Today that logs once and ignores the request (Phase 2 behaviour), and that does not change in this plan. Affected: setup → Sound, setup → Keypad, the `MEDIA`/`PHONE`/`SETUP` hard keys.

## Global Constraints

- Target: `armv7a-linux-gnueabi`, `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp`, glibc **2.13**, `zig` **0.17.1** (pinned in `src/hmi/cmake/zig-armv7-toolchain.cmake`).
- Symbol ceilings in `build/hmi/hmi`: `GLIBC_2.13`, `GLIBCXX_3.4.16`, `CXXABI_1.3.5`. `DT_NEEDED` ⊆ stock `opt/hmi`. Import only Qt symbols that stock imports (`tools/tests/test_hmi_build.py`).
- No new runtime dependency (spec anti-goal). Phase 3a adds no `.so`.
- Source paths and class names follow the recovered stock paths (`docs/decompiled/5624/hmi/hmi_symbols_5624.json` → `recovered_source_files`). A file whose stock name is unknown starts with `// File name is ours; stock name unknown.`
- Put an origin tag `// 0x<8-hex VA> <Class>::<Name>` on the line above every recreated function. Use `// DEVIATION:`, `// STOCK-QUIRK:` and `// TODO(phase3<x>):` where they apply, with deferred work naming its target plan (`TODO(phase3d)` for Sound, etc.). Remove a `TODO(phase3)` marker when its work is done.
- After tags change, run `python3 src/hmi/tools/gen_trace.py`. `gen_trace.py --check` must pass.
- **VMF safety (spec anti-goal):** the HMI sends only tuner commands that match a stock `VAMFMProxy::On*Opera` byte for byte (Task 1 table). No PSS writes in this plan.
- Threading rules from `docs/hmi-internals.md` "Threads and locks" apply. `RecieveVmfMsg` never takes the page mutex and never blocks. Pages run on the `VDisplayTask` thread.
- Never commit `extracted-fw/`, `original-fw/`, `build/`. Every file ends with a newline. Commit after each task, and never use `--no-verify`.

## Review Focus

1. **Short or malformed group `0x6f` messages.** Stock `0xae28c` `memcpy`s fixed sizes (`0xe`, `0x18`, `0x20`, `0x48`) whatever `len` is. That is an over-read when a message is short. Ours copies `min(len-1, size)` and zero-fills the rest (`// DEVIATION:`). A `0x40` list whose length is not a multiple of 12, or whose `[1] != 1`, must not read past `len`. Pinned by `test_proxies.cpp::test_amfm_decoders_short_input` (Task 2).
2. **Leaving the tuner page during seek, scan or auto-store.** Press APP mid-seek, then RADIO. Stock clears the page-active flag (`0xaf364(0)`) and re-requests on Resume. Ours must show the same frames and send the same command trace, with no stale "seeking" indicator. Pinned by the `seek_then_app_then_radio` scenario in `test_hmi_tuner.py` (Task 6).
3. **Tuner status before the page exists.** The boot `0x3d`/`0x3f`/`0x40` replies arrive before `0xf9` starts. `VTunerData` must keep them so that `Start` draws the right station at first paint. Pinned by `test_tuner_data.cpp::test_status_before_page` (Task 2) and the `boot` scenario frame (Task 6).
4. **Band edges and frequency formatting.** FM `8700..10800` (×10 kHz) and AM `531..1602` kHz are the ctor defaults `0x21fc/0x2a30/0x213/0x642` in `0xacfcc`. Formatting must give the stock text at both edges and on every step size. Pinned by `test_tuner_data.cpp::test_format_freq_edges` (Task 2).
5. **Preset index range.** Stock sends `n + 1` for presets 0..5. Store, recall or delete on an empty slot, and indices ≥ 6 from a mis-hit, must neither crash nor send anything out of range. Pinned by `test_amfm_cmd.cpp::test_preset_index_bounds` (Task 1) and `test_controls.cpp::test_presets_bar_index_bounds` (Task 5).

---

## File Structure

```
tools/tests/test_hmi_tuner.py                      # NEW (QEMU): stock-vs-ours scenario parity (frames + command trace)
tools/tests/test_hmi_parity.py                     # boot_pair() factored out for reuse
docs/ipc-and-vgw.md, docs/hmi-internals.md         # tuner protocol notes, tuner area section

src/hmi/control_logic/subsystem/amfm/
  amfm_cmd.{h,cpp}                                 # NEW portable: TX vectors, one builder per stock On*Opera
  vamfmproxy.{h,cpp}                               # full port 0x000acfcc..0x000af3e4
src/hmi/control_logic/subsystem/proxy_decoders.{h,cpp}  # + AM/FM RX decoders, NormalizeTunerPs
src/hmi/display/page/tuner/
  vtunerdata.{h,cpp}                               # NEW portable (name ours): singleton 0x00199144, data 0x00198ef4..0x001993b0
  tuner_format.{h,cpp}                             # NEW portable (name ours): freq/PS text helpers lifted from 0x0012226c / 0x001232e4
  vtunermainpage.{h,cpp}                           # VTunerMainPage + VTunerMainPageImpl_ (0x0011d808..0x001240f8, 0x0012411c)
  vtunersetup.{h,cpp}                              # NEW 0xfa (0x00174344..0x00176064)
  vtunerstationlist.{h,cpp}                        # NEW 0xfb (0x0016b8e4..0x0016f310)
  vtunerstationupdate.{h,cpp}                      # NEW 0xfc (0x001738c8..0x00174090)
  vtunerstorepreset.{h,cpp}                        # NEW 0xfd (0x0017847c..0x001797e0)
  vtunerdeletepreset.{h,cpp}                       # NEW 0xf8 (0x0017a1d8..0x0017a8dc)
  vtunerautostore.{h,cpp}                          # NEW 0xdd (0x001b05bc..)
src/hmi/display/display_msg.{h,cpp}                # NEW portable (name ours): DISPLAY_MSG + PackDisplayMsg (0x00063bd8 clamp)
src/hmi/display/vdisplaytask.{h,cpp}               # + SigSubsystemNotifyMsg / SubsystemNotifyMsgSlot (0x00064d74)
src/hmi/display/page/base/{basepage,pagemanager}.{h,cpp}  # + OnReceiveMsg, DispatchMsg (0x0002b97c / 0x0002b910), tuner refresh
src/hmi/control_logic/vcontroltask.{h,cpp}         # + PostSubsystemMsg (0x0003098c), tuner band/state getters
src/hmi/control_logic/mode/ipcl/vipclnotifyfilter.cpp   # tuner TODOs (scan stop 0x000adbd0, seek on TUNE keys)
src/hmi/display/controls/controlparts/
  vtunerpresetsbar.{h,cpp}                         # NEW 0x002fdc54..0x002ff5d0
  vstationlistbtnctrl.{h,cpp}                      # NEW 0x003057f8..0x00305dd8
  dynamiclist.{h,cpp}, scrollbarcontrol.{h,cpp}, slider.{h,cpp}, vloadingctrl.{h,cpp},
  vdropdownctrl.{h,cpp}, vselectbuttonctrl.{h,cpp}, vcompositetextctrl.{h,cpp}, vswitcherctrl.{h,cpp}
src/hmi/display/controls/controlbase/
  listbase.{h,cpp}, listelem.{h,cpp}, staticlist.{h,cpp}   # NEW
  vcursorlogic.{h,cpp}                             # NEW 0x00305580..0x00305740 (singleton 0x009704d4)
src/hmi/display/controls/list_math.{h,cpp}         # NEW portable (name ours): scroll / indicator / preset-bar maths
src/hmi/display/base/controlcreator.{h,cpp}        # + CreateTunerPresetsBar, CreateDynamicButton, CreateSwitcher, list/slider creators
src/hmi/tests/{test_amfm_cmd,test_tuner_data,test_display_msg}.cpp, test_proxies.cpp, test_controls.cpp, CMakeLists.txt
```

---

### Task 1: Portable AM/FM Command Table (`amfm_cmd`)

**Files:**
- Create: `src/hmi/control_logic/subsystem/amfm/amfm_cmd.{h,cpp}`, `src/hmi/tests/test_amfm_cmd.cpp`
- Modify: `src/hmi/tests/CMakeLists.txt`, `src/hmi/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
struct AmfmCmd { uint8_t len; uint8_t b[7]; };   // b = {0x8f, 0x22, 0x00, n, item, p1, p2}, len = 4 + n; len 0 = send nothing
// One builder per stock sender, e.g.:
AmfmCmd AmfmCmdSeekUp();                       // 0x000ad5d4
AmfmCmd AmfmCmdStep(int steps);                // 0x000ad560 (steps == 0 -> len 0, stock sends nothing)
AmfmCmd AmfmCmdPrestSet(int index);            // 0x000ad9b0 (index 0..5, else len 0)
AmfmCmd AmfmCmdSetFrequency(uint16_t freq);    // 0x000adf18
AmfmCmd AmfmCmdBand(uint8_t band);             // 0x000af164 (len 6)
```
- Builder names follow the stock `On<Name>Opera` minus `On`/`Opera`, keeping stock spelling (`OnScanCancleOpera` → `AmfmCmdScanCancle`). Senders without a log string get a name of ours. **The origin tag sits on the `VAMFMProxy` sender (Task 4). The builder says `// Bytes of 0x… (sender in vamfmproxy.cpp).`** and carries no tag.
- `0x000aef94` (37 bytes) and `0x000af200` (variable, `(n & 0xff) + 4`) don't fit `b[7]`. They stay inline in their Task 4 senders, with their own host test vectors left out.

Vectors (from the decompiled senders). Header `8f 22 00 03` unless noted:

| VA | Builder | bytes after header |
|---|---|---|
| `0xad104` | GetCurrentInfo | header `8f 22 00 01`, `3d` |
| `0xad1a4` | TunerList (ours) | `3b 00 00` |
| `0xad1f0` | GetStaionListInfo(x = proxy `+0x4a`) | `3c x 00` |
| `0xad29c`/`0xad34c` | ManualSeekUp/Dn | `37 00 01` / `37 01 01` |
| `0xad400`/`0xad4ac` | ManualRepeatSeekUp/Dn | `38 00 07` / `38 01 07` |
| `0xad560` | Step(s) (ours) | `37 (s<0)|(abs(s)<<1) 01` |
| `0xad5d4`/`0xad680` | SeekUp/Dn | `37 00 02` / `37 01 02` |
| `0xad738`/`0xad7e8` | SeekStop/SeekCancel | `37 02 00` / `37 03 00` |
| `0xad88c` | (ours)(x) | `33 x ff` |
| `0xad8f4` | (ours)(mode) | `34 …` per the branches of `0xad8f4` |
| `0xad9b0`/`0xada68` | PrestSet/PrestCall(i) | `35 i+1 ff` / `36 i+1 ff` |
| `0xadb2c`/`0xadbd0`/`0xadc80` | Scan/ScanStop/ScanCancle | `40 00 00` / `40 04 00` / `40 06 00` |
| `0xadd30`/`0xadd84` | AutoStore (ours)/AutoStoreCancel | `3e 00 04` / `3e 02 00` |
| `0xade30`/`0xade7c` | InitAutoStore (ours)/InitAutoStoreCancel | `3f 00 00` / `3f 02 00` |
| `0xadf18` | SetFrequency(f) | `65 f>>8 f&ff` |
| `0xadff0`/`0xae094` | DeletePreset(i)/DeletePresetAll | `4a i+1 00` / `49 00 00` |
| `0xae128` | SetSeekmode(m) | `48 {0:00,1:08,2:07} 00` |
| `0xae1f0` | GetSeekmode | `4e 00 00` |
| `0xaeaf4` | SetSortingSTL(m) | `47 {0:00,1:09,2:0a} 00` |
| `0xaebbc` | (ours) | `39 00 00` |
| `0xaec08`/`0xaecb0` | StationOrPresetNext/Previous | `4b 00 00` / `4b 01 00` |
| `0xaed60` | (ours)(p) | `4c p 00` |
| `0xaedb0` | StationlistBack | `3a 00 00` |
| `0xaee44` | StationlistStorePreset(band = proxy `+0x16`, i) | `4d band i+1` |
| `0xaeef8` | ParaSetStart | `5f 00 00` |
| `0xaf070` | FactroyReset | `50 00 00` (sender then calls `0xae1f0`) |
| `0xaf164` | Band(b) | header `8f 22 00 02`, `30 b` |
| `0xaf1b4` / `0xaf298` | (ours) | header `8f 22 00 01`, `31` / `4f` |

- [ ] **Step 1: Write the failing test `test_amfm_cmd.cpp`.** Use one `CHECK_BYTES(cmd, {...})` per table row (a small helper in the test that compares `len` and `b`), plus:

```cpp
static void test_preset_index_bounds() {
    CHECK_EQ(AmfmCmdPrestSet(0).b[5], 1);
    CHECK_EQ(AmfmCmdPrestSet(5).b[5], 6);
    CHECK_EQ(AmfmCmdPrestSet(6).len, 0);     // DEVIATION: stock would send 7; nothing is sent
    CHECK_EQ(AmfmCmdPrestSet(-1).len, 0);
    CHECK_EQ(AmfmCmdPrestCall(6).len, 0);
    CHECK_EQ(AmfmCmdDeletePreset(6).len, 0);
}
static void test_step_encoding() {
    CHECK_BYTES(AmfmCmdStep(1),  {0x8f,0x22,0x00,0x03,0x37,0x02,0x01});
    CHECK_BYTES(AmfmCmdStep(-1), {0x8f,0x22,0x00,0x03,0x37,0x03,0x01});
    CHECK_BYTES(AmfmCmdStep(-3), {0x8f,0x22,0x00,0x03,0x37,0x07,0x01});
    CHECK_EQ(AmfmCmdStep(0).len, 0);
}
```

- [ ] **Step 2: Run it to verify it fails.** Run: `tools/build_hmi.sh --host-tests --no-assets`. Expected: FAIL (`amfm_cmd.h` missing).
- [ ] **Step 3: Implement `amfm_cmd.cpp`**, one builder per row, and register `test_amfm_cmd` with `hmi_add_test`.
- [ ] **Step 4: Run it to verify it passes.** Same command; expected PASS.
- [ ] **Step 5: Commit** `hmi: portable AM/FM command table pinned to stock senders`.

---

### Task 2: AM/FM RX Decoders, `VTunerData`, Frequency/PS Formatting

**Files:**
- Modify: `src/hmi/control_logic/subsystem/proxy_decoders.{h,cpp}`, `src/hmi/tests/test_proxies.cpp`, `src/hmi/tests/CMakeLists.txt`
- Create: `src/hmi/display/page/tuner/vtunerdata.{h,cpp}`, `src/hmi/display/page/tuner/tuner_format.{h,cpp}`, `src/hmi/tests/test_tuner_data.cpp`

**Interfaces:**
- Produces in `proxy_decoders.h`:
```cpp
// The PS loop of 0x000ae28c: trailing spaces -> NUL, leading spaces dropped, NUL-padded to 8.
void NormalizeTunerPs(uint8_t ps[8]);
enum { kAmfmStatusLen = 0xe, kAmfmPresetsLen = 0x48, kAmfmInfoLen = 0x18, kAmfmExtLen = 0x20 };
// data[0] is the item. Copies min(len - 1, size(item)) payload bytes, zero-fills to size(item),
// normalises PS fields (0x3b: ps at +2; 0x3d: ps at +2 when [10] < 3; 0x40: each entry +3,
// whole entries only). Returns false for NULL/empty input and for the 0x3d drop rule
// (proxy_state 0x0c/0x0d and [12] - 0x0c < 2).
bool DecodeAmfmMsg(const uint8_t* data, uint16_t len, uint8_t proxy_state, uint8_t* out, uint16_t* out_len);
```
- Produces in `vtunerdata.h` (portable, `// File name is ours`; stock singleton `0x00199144`, 0x68 bytes, ctor `0x00198ef4`):
```cpp
class VTunerData {
public:
    static VTunerData* Instance();                              // 0x00199144
    void SetStationInfo(const uint8_t raw[kAmfmStatusLen]);     // 0x001992c0 (+0xc, sets +0x62)
    void SetPresets(const uint8_t raw[kAmfmPresetsLen]);        // 0x00199278 (+0x1a)
    const uint8_t* StationInfo() const;
    const uint8_t* Presets() const;
    // Remaining accessors 0x0019931c..0x001993b0, named from their callers.
};
```
- Produces in `tuner_format.h`: `void FormatTunerFreq(uint8_t band, uint16_t freq, char out[16]);` plus the PS/preset text helper that `0x0012226c` / `0x001232e4` build, lifted as pure functions (helpers derived from those VAs, no tag).

- [ ] **Step 1: Write the failing tests.** In `test_proxies.cpp`:

```cpp
static void test_tuner_ps_normalize() {
    CHECK_PS("  BBC R1", "BBC R1\0\0");
    CHECK_PS("        ", "\0\0\0\0\0\0\0\0");
    CHECK_PS("  X  Y  ", "X  Y\0\0\0\0");
    CHECK_PS("ANTENNE ", "ANTENNE\0");
}
static void test_amfm_decoders_short_input() {
    uint8_t out[0x100]; uint16_t n = 0;
    const uint8_t st[3] = {0x3d, 0x10, 0x27};                    // 2 of 14 payload bytes
    CHECK(DecodeAmfmMsg(st, 3, 0, out, &n)); CHECK_EQ(n, kAmfmStatusLen); CHECK_EQ(out[2], 0);
    uint8_t lst[2 + 12 + 5] = {0x40, 0x01};                      // one entry + 5 stray bytes
    CHECK(DecodeAmfmMsg(lst, sizeof(lst), 0, out, &n)); CHECK_EQ(n, 1 + 12);   // item byte dropped, partial entry dropped
    CHECK(!DecodeAmfmMsg(0, 5, 0, out, &n));
    CHECK(!DecodeAmfmMsg(st, 0, 0, out, &n));
}
static void test_amfm_status_drop_rule() {
    // proxy_state 0x0c, [12] = 0x0d -> dropped; [12] = 0x0e -> kept; proxy_state 0 -> kept.
}
```

  In `test_tuner_data.cpp`:
  - `test_status_before_page`: set the status and presets, then read them back unchanged with `+0x62` set.
  - `test_format_freq_edges`: FM 8700, 8750, 10800; AM 531, 1602; one 9 kHz AM step. Copy the expected strings from the text that `0x0012226c` builds, and confirm them against a stock QEMU frame.

- [ ] **Step 2: Run to verify fail.** `tools/build_hmi.sh --host-tests --no-assets` gives FAIL.
- [ ] **Step 3: Implement** the decoders (with `// DEVIATION:` on the bounded copy), `VTunerData` and `tuner_format`. Register `test_tuner_data`.
- [ ] **Step 4: Run to verify pass**, then `python3 src/hmi/tools/gen_trace.py`.
- [ ] **Step 5: Commit** `hmi: AM/FM RX decoders, tuner data store and frequency formatting`.

---

### Task 3: DISPLAY_MSG Path (Proxy → VControlTask / VDisplayTask → Pages)

Phase 2 has no route from a proxy to a page (`vdisplaytask.cpp:46` TODO). Every Phase 3 area needs it, and the tuner is the first to use it.

**Files:**
- Create: `src/hmi/display/display_msg.{h,cpp}`, `src/hmi/tests/test_display_msg.cpp`
- Modify: `src/hmi/display/vdisplaytask.{h,cpp}`, `src/hmi/display/page/base/{basepage,pagemanager}.{h,cpp}`, `src/hmi/control_logic/vcontroltask.{h,cpp}`, `src/hmi/tests/CMakeLists.txt`, `src/hmi/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
// display_msg.h (portable). Stock DISPLAY_MSG, 0x1008 bytes, passed by value through queued signals.
struct DISPLAY_MSG { int32_t type; uint32_t len; uint8_t grp; uint8_t sub; uint8_t data[0xffe]; };
enum { kDisplayMsgVmf = 7 };
// 0x00063bd8 body: type 7, len = min(n + 2, 0xfff), payload clamped to 0xffe. data == NULL -> out untouched, false.
bool PackDisplayMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t n, DISPLAY_MSG* out);
```
  - `VDisplayTask::PostSubsystemMsg(uint8_t grp, uint8_t sub, const uint8_t* data, uint16_t len)` (`0x00063bd8`) emits `SigSubsystemNotifyMsg(DISPLAY_MSG)`. That is queued to `SubsystemNotifyMsgSlot(DISPLAY_MSG)` (`0x00064d74`), which calls `VPageManager::DispatchMsg(const DISPLAY_MSG&)` (`0x0002b97c` → `0x0002b910`). The moc signature strings must stay exactly stock (connects in `0x00065238`).
  - `VPageManager::DispatchMsg` walks layers from `m_top` down to 0 and calls `OnReceiveMsg` on each entry that is active and has a page. It returns immediately when `m_force_show` (`+0xf2`) is set or `m_top > 0x1c`. It holds the page mutex.
  - `virtual void VBasePage::OnReceiveMsg(const DISPLAY_MSG& msg)` (vtable `+0x100`). The base does nothing.
  - `VControlTask::PostSubsystemMsg(grp, sub, data, len)` (`0x0003098c`) feeds the existing notify-filter chain. Port the routing for group `0x6f`; other groups keep their Phase 2 behaviour.
  - The `0x00064d74` special case (type 3, `0x0c`/`0x06` → gesture flag `0x00064cf0`) stays a TODO that names its owning area plan.

- [ ] **Step 1: Write the failing test `test_display_msg.cpp`**:
  - `n = 0` gives len 2.
  - `n = 10` gives len 12, with the bytes copied and `grp`/`sub` set.
  - `n = 0xffd` gives len `0xfff`.
  - `n = 0xffe` gives len `0xfff` (clamped), with all `0xffe` bytes copied.
  - `n = 0x1200` copies `0xffe` bytes.
  - `data == NULL` returns false and leaves `out` untouched.
- [ ] **Step 2: Run to verify fail** (`--host-tests --no-assets`).
- [ ] **Step 3: Implement** `display_msg.cpp`, the signal/slot pair (register the metatype the way stock does, using only stock-imported Qt symbols; the build gate checks this), `DispatchMsg`, `OnReceiveMsg` and `VControlTask::PostSubsystemMsg`.
- [ ] **Step 4: Verify.** Run `tools/build_hmi.sh` (ABI gate PASS) and `--host-tests` (PASS). Then run `RCD330_QEMU_TESTS=1 python3 -m unittest tools.tests.test_hmi_boot tools.tests.test_hmi_interactive -v` (PASS).
- [ ] **Step 5: Commit** `hmi: DISPLAY_MSG path from proxies to VControlTask and active pages`.

---

### Task 4: Full `VAMFMProxy` Port

**Files:**
- Modify: `src/hmi/control_logic/subsystem/amfm/vamfmproxy.{h,cpp}`, `src/hmi/control_logic/vcontroltask.cpp` (`:557`, `:562` getters), `src/hmi/control_logic/mode/ipcl/vipclnotifyfilter.cpp` (tuner TODOs incl. `0x000adbd0`)
- Create: `tools/tests/test_hmi_tuner.py` (only the `vmf_trace_boot` test; Task 6 extends it)

**Interfaces:**
- Consumes: Task 1 builders, Task 2 `DecodeAmfmMsg` / `VTunerData`, Task 3 `PostSubsystemMsg` on both tasks, and the VIP tuner peer / `TUNER_TRACE` from Emulator Plan 3 Task 5.
- Produces:
  - The ctor `0x000acfcc` with every stock member.
  - Every sender in Task 1's table, with the stock parameter list (`void OnSeekUpOpera();`, `void OnPrestCallOpera(char index);`, …), plus `0x000aef94` and `0x000af200` inline.
  - Getters: `uint8_t GetTunerBand() const` (`0x000af314`, `+0x12`), `uint8_t GetTunerState() const` (`0x000af304`, `+0x14`), `void SetTunerPageActive(uint8_t on)` (`0x000af364`, `+0x3b`). The other `0x000af2f8..0x000af3e4` accessors are named from their callers (`// Name is ours.`).
  - `RecieveVmfMsg` (`0x000ae28c`) decodes into a local copy, because stock normalises the VMF buffer in place and ours is `const`. Per item it updates the proxy and `VTunerData` exactly as stock does:
    - `0x3b`: only when `0x00207eb4() == 1`.
    - `0x3d`: the page `0xf9` band-changing flag via `0x0012168c` (Task 6 provides it; until then the cast finds no page), and the seek-mode refresh `0x000ae1f0`.
    - `0x3e`: keep the stock log line verbatim.
    - `0x3f`: including the audio-mode branch `0x00046b38`/`0x00054fcc`/`0x000500b4`.
    - `0x40`, `0x42`, `0x43` (log kept), `0x44`.

    It then forwards to `VControlTask::PostSubsystemMsg` and `VDisplayTask::PostSubsystemMsg`.
- `0x00207dd4`/`0x00207eb4` belong to the ETM version area. Port only the accessor that `0x3b` needs, with `TODO(phase3h)`.

- [ ] **Step 1: Write the failing check** `test_hmi_tuner.py::test_vmf_trace_boot`. Boot stock, then ours, each with `tuner_trace(True)` set before `wait_hmi_ready()`. The ordered `EVT TUNERCMD` payload lists must be equal, and both serial logs must show `VAMFMProxy::RecieveVmfMsg: m_uSeekModeType=` with the same value.
- [ ] **Step 2: Run to verify fail.** `RCD330_QEMU_TESTS=1 python3 -m unittest tools.tests.test_hmi_tuner -k vmf_trace_boot -v` gives FAIL (ours drops RX and sends fewer commands).
- [ ] **Step 3: Implement** the port. Remove the `m_logged` stub and the `TODO(phase3)` markers it resolves.
- [ ] **Step 4: Verify.** The QEMU test passes. Run `--host-tests` and `gen_trace.py`. `test_hmi_interactive` stays green: `HandleLPCLHardKeyTuneUp/Down` still log, and they now send the same step commands as stock (compare traces if the test is extended).
- [ ] **Step 5: Commit** `hmi: full VAMFMProxy (all senders, RX handler, getters)`.

---

### Task 5: Tuner Controls (Presets Bar, Slider, Lists, Cursor) and Their Creators

**Files:**
- Create: the control files listed under `controlparts/` and `controlbase/` in the File Structure, plus `src/hmi/display/controls/list_math.{h,cpp}`
- Modify: `src/hmi/display/base/controlcreator.{h,cpp}`, `src/hmi/display/controls/controlparts/igdtbutton.cpp` (`:55`, `:73` cursor TODOs), `src/hmi/tests/test_controls.cpp`, `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Produces these controls at their stock VA spans (recovered file → class):

  | Class | VA span |
  |---|---|
  | `VTunerPresetsBar` (signals as stock moc strings) | `0x002fdc54..0x002ff5d0` |
  | `Slider` | `0x002e07e8..0x002e10ac` |
  | `ListBase` | `0x002d17fc..0x002d1e74` |
  | `ListElem` | `0x002d206c..0x002d26e0` |
  | `StaticList` | `0x002d2e44..0x002d5e9c` |
  | `DynamicList` | `0x002c9e88..0x002cc5a0` |
  | `ScrollbarControl` | `0x002e1e04..0x002e27a8` |
  | `VStationListBtnCtrl` | `0x003057f8..0x00305dd8` |
  | `VCursorLogic` (singleton `0x009704d4`) | `0x00305580..0x00305740` |
  | `VLoadingCtrl` | `0x002e7754` |
  | `VDropdownCtrl` | `0x002df1a0..0x002e0244` |
  | `VSelectButtonCtrl` | `0x002e074c` |
  | `VCompositeTextCtrl` | `0x002f6b88..0x002f79f4` |
  | `VSwitcherCtrl` | `0x002e96ec`, `0x002fe5e8` |

  Port only the functions the seven tuner pages reach. Members that no tuner page calls get `TODO(phase3<x>)` naming the first area that needs them.
- Produces in `ControlCreator`:
  - `CreateTunerPresetsBar` (`0x002d9c94`), `CreateDynamicButton` (`0x002d94c8`) and `CreateSwitcher` (`0x002d82b4`).
  - The creators the tuner page constructors call: `0x002d78d8`, `0x002d79b0`, `0x002d8258`, `0x002d8800`, `0x002d8c94`, `0x002d8cf8`, `0x002d8f74`, `0x002d9a68`, `0x002dafec`. Each is named after what it creates, with `// Name is ours.` where stock's name is not recovered.
- Produces in `list_math.h` (portable, helpers derived from the cited VAs):
```cpp
int ListScrollTo(int index, int count, int visible);                         // ListBase::ScrollTo
int ScrollIndicatorPos(int first, int count, int visible, int track_h, int ind_h);  // 0x002e1e04
int PresetsBarIndexAt(int x, int bar_x, int slot_w, int slots);              // 0x002ff084; -1 outside
```

- [ ] **Step 1: Write the failing tests in `test_controls.cpp`:**
  - `test_list_scroll_math`: first, last, over-scroll, and count < visible.
  - `test_scroll_indicator_pos`: top, bottom and middle, with values worked out from `0x002e1e04` using the station-list geometry from the generated tables.
  - `test_presets_bar_index_bounds`: x left of the bar → -1, the last pixel of slot 5 → 5, past it → -1, slots = 6.
- [ ] **Step 2: Run to verify fail** (`--host-tests`).
- [ ] **Step 3: Implement** the maths, the controls and the creators. These controls appear on screen only once the Task 6+ pages use them.
- [ ] **Step 4: Verify.** `--host-tests` PASS, `tools/build_hmi.sh` PASS, `gen_trace.py`, and the boot / interactive QEMU tests still PASS.
- [ ] **Step 5: Commit** `hmi: tuner controls (presets bar, slider, lists, cursor, dropdown) and creators`.

---

### Task 6: Tuner Main Page (`0xf9`) and the Tuner Parity Harness

**Files:**
- Modify: `src/hmi/display/page/tuner/vtunermainpage.{h,cpp}`, `src/hmi/display/page/base/pagemanager.cpp` (`:167` tuner refresh `0x0011e280`/`0x0011e434`), `src/hmi/display/igdt/extend/igdt_wrapper.cpp` (`:501`, the show-skip list naming `0xf9`/`0xfb`), `src/hmi/control_logic/input/vknobdriver.cpp` (tuner TODOs)
- Modify: `tools/tests/test_hmi_parity.py` (factor out `boot_pair()`), `tools/tests/test_hmi_tuner.py`

**Interfaces:**
- Consumes: Tasks 2–5 and Emulator Plan 3 Task 5 (`Rcd330Qemu` `tuner_trace`/`tuner_events`/`drag`).
- Produces:
  - `VTunerMainPage` (ctor `0x0012218c`, `0x70` bytes) owning `VTunerMainPageImpl_` (ctor `0x0012411c`).
  - Every function in `0x0011d808..0x001240f8` listed under `vtunermainpage.cpp` in `recovered_source_files`: `CreateButton` ×2, `ChangeTunerBand` ×2, `RREUp/Dn/PushHandler`, `OnUpdating` ×2, `OnTune{Press,Leave,Clicked,LongPress,BtnPress,BtnRelease,FastTimeOut}`, `OnBankIndexChanged`, `OnTuneSlidingIndiClicked`, `IsTunerBandChanging`/`SetTunerBandChangingFlag` (`0x0012168c`), `OnPushUp{AM,FM}Clicked`, `ReceiveMsg` (`0x00121ad8`, reached through `OnReceiveMsg`), `UpdateStationInfo`, `UpdatePresetsInfo`, `Start`/`Stop`/`Pause`/`Resume`, and `MFLNextLongPressHandler` (`0x000658a4`).
  - The Phase 2 `TODO(phase3)` notes in `vtunermainpage.cpp` are resolved.
- Produces in Python:
  - `test_hmi_parity.boot_pair(scenario) -> dict[str, (stock_frame, ours_frame)], (stock_trace, ours_trace)`. It holds the existing language-race handling (re-boot stock up to `STOCK_ATTEMPTS`), sets the clock (`CLOCK`) and tuner trace, and runs each `(name, action)` step on both HMIs, grabbing a stable frame after each one. `HmiParityTest` is reworked to use it, with unchanged behaviour.
  - In `test_hmi_tuner.py`: `touch_center(screen_id, tag)` from the `gen_igdt_tables` touch paths. Each step must score `compare(...) >= MIN_MATCH` (0.995 at ±4, unmasked), and the full `EVT TUNERCMD` lists must be equal.

- [ ] **Step 1: Write the failing scenario in `test_hmi_tuner.py`:**

```python
SCENARIO_MAIN = [
    ("boot",                     lambda q: None),          # wait_hmi_ready: boot page is 0xf9 (Review Focus 3)
    ("radio",                    lambda q: q.press("RADIO")),
    ("knob_up_3",                lambda q: q.knob(3)),
    ("seek_up",                  lambda q: q.tap(*touch_center(0xf9, SEEK_UP_TAG))),
    ("preset_2_recall",          lambda q: q.tap(*touch_center(0xf9, PRESET_TAGS[2]))),
    ("preset_4_store_long",      lambda q: q.touch(*touch_center(0xf9, PRESET_TAGS[4]), hold_s=2.5)),
    ("band_am",                  lambda q: q.tap(*touch_center(0xf9, BAND_AM_TAG))),
    ("band_fm",                  lambda q: q.tap(*touch_center(0xf9, BAND_FM_TAG))),
    ("seek_then_app_then_radio", seek_app_radio),          # Review Focus 2
]
def test_main_page_frames_and_trace(self): ...
```

  Read the tags from the page code (the `CreateButton` touch ids in `0x0012411c`) and record each with its VA in a comment.

- [ ] **Step 2: Run to verify fail.** `RCD330_QEMU_TESTS=1 python3 -m unittest tools.tests.test_hmi_tuner -v` gives FAIL at `boot`/`radio` (ours draws the empty shell).
- [ ] **Step 3: Implement** the page and impl, the page-manager tuner refresh, the knob routing and the wrapper show-skip for `0xf9`.
- [ ] **Step 4: Verify.** Every scenario step passes, and so do the Phase 2 parity and interactive tests. Update `test_hmi_interactive.py` only where stock behaviour now differs from the Phase 2 shell (`assert_same_frame(tuner_ppm, radio_ppm)` may need the tuner frame taken after RADIO), and keep its review-focus checks.
- [ ] **Step 5: Commit** `hmi: tuner main page (0xf9) and stock-vs-ours tuner scenario parity`.

---

### Task 7: Station List (`0xfb`) and Station Update (`0xfc`)

**Files:**
- Create: `src/hmi/display/page/tuner/vtunerstationlist.{h,cpp}`, `vtunerstationupdate.{h,cpp}`
- Modify: `src/hmi/display/page/base/pagemanager.cpp` (ShowPage cases `0xfb` → `0x7c8` bytes / ctor `0x0016f310`; `0xfc` → `0x90` / `0x001738c8`), `tools/tests/test_hmi_tuner.py`, `src/hmi/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `VTunerStationListPage`: `TouchScreenEvent` (`0x0016b8e4`), `UpdateActiveItem`, `UpdatStationList` (stock spelling), `IsListDataReady`, `BindDataToItem`, `OnUpdateTunerDynamicList` (`0x0016ed58`), the ctor, and the rest of `0x0016b000..0x001738c8` found by caller.
  - `VTunerStationUpdatePage`: the ctor, `Start` (`0x00174090`) and the `VLoadingCtrl`.
- The list uses `DynamicList`/`VStationListBtnCtrl`/`ScrollbarControl` from Task 5. Sorting goes through `AmfmCmdSetSortingSTL`, and selecting a station through the stock sender the page calls.

- [ ] **Step 1: Add the failing scenario** `SCENARIO_LIST`:
  1. Open the station list from main, using the stock entry point that leads to `ShowPage 251`.
  2. Take the list frame.
  3. Scroll down with the knob.
  4. Drag-scroll by touch (`Rcd330Qemu.drag`, provided by Emulator Plan 3 Task 5).
  5. Select entry 3: check the frame and the tune command in the trace.
  6. Update the list: the `0xfc` loading frame, then the list again after the VIP's `0x40`.
  7. Back.
- [ ] **Step 2: Run to verify fail.** FAIL (ours ignores `ShowPage 251`).
- [ ] **Step 3: Implement** both pages and the two ShowPage cases.
- [ ] **Step 4: Verify.** All tuner scenarios pass, and so do `--host-tests` and `gen_trace.py`.
- [ ] **Step 5: Commit** `hmi: tuner station list (0xfb) and station update (0xfc) pages`.

---

### Task 8: Tuner Setup (`0xfa`)

**Files:**
- Create: `src/hmi/display/page/tuner/vtunersetup.{h,cpp}`
- Modify: `pagemanager.cpp` (case `0xfa` → `0x90` / ctor `0x00176064`), `tools/tests/test_hmi_tuner.py`, `src/hmi/CMakeLists.txt`

**Interfaces:**
- Produces: `VTunerSetupPage` with `hmi_tuner_setup_present_handler` (`0x00174344`), `PopFront`, `UpdateSeekModeData`, `OnBtn{Scan,Sound,Arrow,DeletePreset,PresetLists,StationList,Keypad}Clicked`, `InitiatData`, `Start`, and the ctor.
- Sound and Keypad call `ShowPage` with their stock screen ids. Those screens stay unimplemented (log once) and are marked `TODO(phase3d)` (Sound) and `TODO(phase3e)` (Keypad).
- Seek mode is set through `AmfmCmdSetSeekmode` and refreshed from `0x43`.

- [ ] **Step 1: Add the failing scenario** `SCENARIO_SETUP`:
  1. Open setup and take the frame.
  2. Open the seek-mode dropdown and select an entry: frame, plus `48 xx 00` in the trace.
  3. Scan: frame, plus `40 00 00`. Main shows the scan state. Stop the scan with the TUNE_OK hard key, which sends `40 04 00`.
  4. Sound: both HMIs log the ShowPage request, and ours logs "not implemented" once. This step **compares the trace only**. The test names the expected frame divergence until 3d.
  5. Back.
- [ ] **Step 2: Run to verify fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Verify.** The scenarios pass (the Sound frame is excluded by name, with the reason in the test), and so does `--host-tests`.
- [ ] **Step 5: Commit** `hmi: tuner setup page (0xfa)`.

---

### Task 9: Preset Pages — Store (`0xfd`), Delete (`0xf8`), Auto-Store (`0xdd`)

**Files:**
- Create: `src/hmi/display/page/tuner/vtunerstorepreset.{h,cpp}`, `vtunerdeletepreset.{h,cpp}`, `vtunerautostore.{h,cpp}`
- Modify: `pagemanager.cpp` (cases `0xfd` → `0xd4` / `0x0017847c`, `0xf8` → `0xf0` / `0x0017a8dc`, `0xdd` → `0xb8` / `0x001b05bc`), `tools/tests/test_hmi_tuner.py`, `src/hmi/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `VTunerStorePresetPage`: the ctor, `UpdatePresetData` (`0x00179444`) and `Start` (`0x001797e0`).
  - `VTunerDeletePresetPage`: `UpdatePresetData` (`0x0017a1d8`), `0x0017a69c` and the ctor (`0x0017a8dc`).
  - `VTunerAutoStorePage`: the ctor (`0x001b05bc`) and its members up to the next recovered class.
- These pages read presets only through `VTunerData`, and send only Task 1 builders (`PrestSet`, `DeletePreset`, `DeletePresetAll`, `AutoStore`, `AutoStoreCancel`).

- [ ] **Step 1: Add the failing scenario** `SCENARIO_PRESETS`:
  1. Store the current station at slot 1 from the store page.
  2. Delete slot 1; the main presets bar is then empty at 1.
  3. Confirm delete-all.
  4. Start auto-store (`0xdd` frame) and let it complete: the VIP sends the list and presets, and main shows 6 presets.
  5. Cancel auto-store mid-run (`3e 02 00`).
  6. Recall an empty slot (Review Focus 5). The frame and the trace must equal stock's, whatever stock does.
- [ ] **Step 2: Run to verify fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Verify.** All tuner scenarios pass, and so do `--host-tests` and `gen_trace.py`.
- [ ] **Step 5: Commit** `hmi: tuner preset pages (store 0xfd, delete 0xf8, auto-store 0xdd)`.

---

### Task 10: Area Close-Out — Docs, Trace, Full Regression

**Files:**
- Modify:
  - `docs/hmi-internals.md`: a new "Tuner area" subsection covering screens, proxy items, state codes, the DISPLAY_MSG path and the deviations taken.
  - `docs/ipc-and-vgw.md`.
  - `src/hmi/README.md`: a layout row for `display/page/tuner/`, plus the `test_hmi_tuner` / `test_vip_tuner` commands.
  - `src/hmi/TRACE.json`.
  - `AGENTS.md`, only if a repo-map line changes.

- [ ] **Step 1: Check for stale markers.** This command must print nothing:
  `grep -rn 'TODO(phase3)' src/hmi/control_logic/subsystem/amfm src/hmi/display/page/tuner src/hmi/display/controls`
  Every remaining phase-3 TODO in those files names its target plan (`TODO(phase3d)` …).
- [ ] **Step 2: Run the full gate set.**
  - `tools/build_hmi.sh --force` → PASS (ABI, Qt-import and `DT_NEEDED` gates)
  - `tools/build_hmi.sh --host-tests` → all `ctest` PASS, including the asset sweep
  - `python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v` → PASS
  - `python3 src/hmi/tools/gen_trace.py --check` → PASS
  - `RCD330_QEMU_TESTS=1 python3 -m unittest discover -s tools/tests -p "test_*tuner*.py" -v` and the Phase 2 QEMU suites → PASS
  - Spec §5.8, PQ and MQB: run `test_hmi_tuner` again with the MQB VRM/coding profile. Add a `RCD330_CODING_PROFILE=mqb` switch to `boot_pair()`. With Emulator Plan 1 Task 3 landed, prefer passing a temporary MQB `--coding-file` (setting `rcd330-vip` qdev `platform`/`coding-hex` at startup); otherwise apply `VRM` + `CODING` (`tools/rcd330_coding.py`) before `wait_hmi_ready`. Expected: PASS under both profiles.
- [ ] **Step 3: Update the docs** listed above, and move this plan to `docs/superpowers/plans/completed/`.
- [ ] **Step 4: Commit** `docs: phase 3a tuner area complete`.
