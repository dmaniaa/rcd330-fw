# `opt/hmi` Source Recreation — Design

Date: 2026-11-24 · Status: draft for approval · Location: `src/hmi/`

## 1. Intent

Rebuild the stock `5624_vw` `opt/hmi` (ARMv7, 9,580 functions, stripped) as readable, buildable C++ source that:

1. **Matches stock look, feel and function exactly** — same screens, transitions, input behavior, VMF traffic.
2. **Is a drop-in replacement** for `/opt/hmi` on a stock `5624` rootfs — same process name, same launcher (`pwr_agent`), same shared libraries, same IPC. Eventually flashed onto real cars.
3. **Serves as a readable reference** to the decompilation: every recreated function traceable to its `docs/decompiled/hmi/functions/0x*.c` origin.
4. **Is the base of the user's future alternative headunit software** — new screens and features get added on top later.

Why: the user wants a platform they fully own for future alternative software, but starting from proven, exact stock behavior rather than a redesign.

## 2. Goals / Non-goals / Anti-goals

**Goals**
- Full feature parity with stock `opt/hmi` 5624 (all 18 subsystem proxies, all pages incl. 85 popups, ETM/engineering, EOL test, CarPlay/AA/CarLife/MirrorLink pages, camera).
- Two skins on the **5624 layouts and page code**: **VW** (stock 5624 look) and **Skoda** (5624 layouts with the Skoda look — 5606 textures, colors, fonts applied). The Skoda skin is *not* a port of the 5606 layouts/page code.
- Six languages now: `en_US`, `zh_CN`, `pt_BR`, `ru_RU`, `es_MX` (5624 string block) + `de_DE` (from `resources/translations/de_DE.json`, originating from the 5606 patched slot). Language support is **extensible to as many languages as practical** — more community translations will be added later.
- Original source tree layout recreated from the 333 distinct `.cpp` paths found in the 5624 binary (`display/...`, `control_logic/...`, `service/...`, `camera_detect/...`, `mlc_cb_ctrl/...`).
- Modular along the original seams: proxy ↔ notify filter ↔ page manager ↔ page ↔ controls ↔ IGDT.

**Non-goals**
- Recreating other binaries now (`vgw`, `media`, …). If ever done: each in its own `src/<binary>/`.
- Improving/redesigning stock behavior during parity work. Bugs/quirks are reproduced and marked `// STOCK-QUIRK:`; fixes come later as explicit changes.
- Reproducing stock binary layout, addresses, or byte-identical output.

**Anti-goals** (failure even if it "works")
- Any new runtime dependency on the device (no Qt5/6, no QML, no new `.so` beyond what stock `opt/hmi` already `NEEDED`s).
- Losing the 1:1 traceability to the decompilation (e.g. merging/redesigning classes during parity work).
- Anything that could leave a car without a working head unit (crash loops, wrong VMF messages to `vgw`/VIP, writing bad PSS/coding data).

## 3. Constraints (verified)

- **Linked libraries** (from `opt/hmi` `DT_NEEDED`): `libGAL`, `libEGL.so.1`, `libGLESv2.so.2`, `libts-1.0.so.0`, `libfreetype.so.6`, `libximage.so.1`, `libHAL.so.1`, `libvmf_client.so`, `libmlc_iface.so`, `libthreadutil.so`, `libupnp.so`, `libaupnp.so`, `libcurl.so.4`, `libcrypto.so.0.9.8`, `libexpat.so.0`, `libz.so.1`, `libixml.so`, `libusb-1.0.so.0`, `libasound.so.2`, `libOSAL.so.1`, `libQtCore.so.4` (Qt Embedded 4.7.4, `/usr/local/qte4.7.4`), `libdbus-1.so.3`, `libpthread`, `libstdc++.so.6` (**6.0.16 → GCC 4.6 ABI**), `libm`, `libgcc_s`, `libc`. The recreation may use only this set (a subset is fine).
- **Toolchain**: macOS host, `zig` available (no `arm-linux-gnueabi-gcc`). Target `armv7a-linux-gnueabi`, `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp`, glibc 2.13 symbol versions — same as `tools/build_shims.sh`. Must link against the device's own `libstdc++.so.6.0.16` (old ABI: `-D_GLIBCXX_USE_CXX11_ABI=0`, C++98/03-compatible headers matching GCC 4.6 libstdc++).
- **Qt 4.7.4**: no headers on device. Need Qt 4.7.4 headers and a host-built `moc` (Qt 4.7.4 `src/tools/moc`) at build time only.
- **IGDT** is statically linked in stock (`0x31b000..0x32a500`) → must be reimplemented in `src/hmi/display/igdt/`.
- **Scene/asset/string data** in stock is baked into `.rodata` tables (see `docs/hmi-internals.md` §6, `docs/assets-and-localization.md`). Extracted forms exist: `resources/scenes/scenes_{5624_vw,5606_skoda}.json`, `resources/translations/*.json`, `docs/decompiled/hmi/gresfiles_catalog_{5624,5606}.json`. Bitmaps (`_ovg.bin`, 2,825 files VW / 2,626 Skoda) and fonts live in `/opt/gresfiles/` on device.
- **Target rootfs: stock 5624 only** (latest firmware, latest bugfixes). Stock 5606 units are out of scope; a Skoda car runs the 5624 rootfs with this HMI's Skoda skin.
- **Stock 5624 has no Skoda textures**; Skoda skin requires shipping the 5606 `gresfiles` set alongside. 5606 element trees differ from 5624 (e.g. MediaBrw `0x4f`: 31 vs 254 elements) — Skoda assets are mapped onto 5624 elements; where a 5606 texture doesn't fit a 5624 element size, the skin layer picks the closest Skoda asset or falls back to tinting the VW asset (`// DEVIATION:`).
- **Stock language selector has 5 slots (`0..4`)** and the slot value travels outward (VMF to media/BT, PSS, possibly BAP to the cluster). Extending it is a deliberate deviation that must stay inside the HMI.
- **Scene data coverage**: `resources/scenes/*.json` holds only 14 of 255 screens per build; the full tables must be read from the stock binaries.
- Never commit `original-fw/`, `extracted-fw/`, `build/` (firmware-derived binaries/assets stay out of git; generated data is produced at build time from local extraction).

## 4. Approaches considered

| | Approach | Trade-off |
|---|---|---|
| **A (chosen)** | **Structural port**: class-by-class rebuild of the original architecture under recovered source paths, same libs/ABI, IGDT reimplemented, scenes from extracted tables. | Most upfront work, inherits stock quirks; but only option giving exact parity + 1:1 reference + drop-in at once. |
| B | Same runtime, new internal architecture (e.g. single event bus). | Cleaner for future features, but loses reference value and parity must be re-proven without guidance. |
| C | Drop QtCore, own event loop on libc/libstdc++. | Fewer deps, but rewrites every QObject/signal/timer/thread use → drifts from decompilation; no gain since QtCore 4 is on every unit. |

Rejected outright: Qt5/6/QML (not on device → not drop-in).

User picked **A**, and added: any other remade binary goes in its own `src/<binary>/` directory.

## 5. Design

### 5.1 Source tree

```
src/hmi/
  CMakeLists.txt, cmake/zig-armv7-toolchain.cmake
  main.cpp                         # process entry, task startup order as stock
  control_logic/vcontroltask.cpp
  control_logic/input/{vkeydriver,vknobdriver}.cpp
  control_logic/mode/{audio,camera,display,ipcl,media,power}/v*filter.cpp
  control_logic/subsystem/<18 subsystems>/v*proxy.cpp
  service/utility/vbaseproxy.cpp   # VMF framing, RegisterVmfGroup/SendVmfMsg/PackMsg
  display/vdisplaytask.cpp
  display/base/controlcreator.cpp
  display/controls/{controlbase,controlparts}/...
  display/igdt/                    # IGDT reimplementation (engine core + extend/)
    extend/{igdt_wrapper,igdt_imageprocess,ui_auto_add_line}.cpp
  display/multitouch/vmultitouch.cpp
  display/page/{base,tuner,media,phone,phone_setup,system_setup,car,sound,service,
                popup,etm,appconnect,mirrorlink,carplay,carlife,climate,keypad,
                system_time_calendar,eol_test}/...
  camera_detect/vcameratask.cpp
  mlc_cb_ctrl/vmlc*task.cpp
  skin/                            # NEW (not in stock): skin + language selection glue
  third_party/qt-4.7.4-headers/    # or fetched at build time (builder's call)
  tools/gen_*.py                   # build-time generators: scenes/strings/tex tables → C++
  tests/                           # host-run unit tests for pure-data code
```

Directories, file names and class names follow the 318 recovered paths exactly. Files whose original names are unknown (e.g. IGDT engine core, unnamed helpers) get descriptive names and a header note saying the name is ours.

### 5.2 Traceability convention

Every recreated function carries a one-line origin tag: `// 0x0005ff4c VBaseProxy::SendVmfMsg` (5624 VA). Deviations from stock carry `// DEVIATION: <why>`; reproduced stock bugs carry `// STOCK-QUIRK: <what>`. A generated index (`src/hmi/TRACE.md` or JSON) maps VA → file:line, so coverage of the 9,580 functions is measurable.

### 5.3 Data: scenes, textures, strings

Build-time generators parse the stock `.rodata` tables directly from the local `extracted-fw/5624_vw/opt/hmi` (and `extracted-fw/5606_skoda/opt/hmi` for Skoda texture descriptors/colors), using the VAs in `docs/hmi-internals.md` §6, plus `resources/translations/*.json` for strings, and emit C++ constant tables equivalent to the stock `.rodata` tables (`scr_table`, `tex_table`, group/img/txt node tables, `text_layout`, string block) — **one table set per skin**, compiled into the single binary. At runtime the active skin selects the table set and the gresfiles root (`/opt/gresfiles` for VW, a separate directory for Skoda — exact path is the builder's call). Pages keep referring to screens/elements by stock IDs (`eScrId`, element indices), so page code is skin-agnostic exactly as stock code is brand-agnostic between the two firmware builds.

Scene/layout tables are always the 5624 ones; a skin is an asset/color/font overlay keyed by 5624 texture slot (Skoda: a generated 5624-slot → 5606-asset map, built by matching asset names/roles, with manual overrides where names diverge). Page logic exists once.

### 5.4 Skin and language selection

- **Skin** chosen at startup from the VRM brand byte (`docs/coding.md` §5.1: `1 = SKODA`, `2 = VW`), overridable for development (env var or PSS key — builder's call). Default VW.
- **Language**: a generated language table (one entry per `resources/translations/<locale>.json`: locale, display name, font, outward code) replaces the fixed 5-slot array; adding a language = dropping in a translation file and rebuilding. Selector UI scrolls if entries exceed one page. Internally the HMI uses its own index; **outward** (VMF, PSS, BAP) it sends only stock-valid codes via the table's outward-code column (languages unknown to stock map to a safe stock code, e.g. `en_US`) — `// DEVIATION:`. Before implementing, the decompilation must be checked for every consumer of the slot value to confirm the mapping is safe. Own persisted index stored separately from the stock slot. Language switch path otherwise identical to stock (`VSystemSetupMainPage::UpdateAllText`, media notify, glyph cache refresh).

### 5.5 Rendering (IGDT reimplementation)

Reimplement the IGDT engine behaviors the HMI uses: scene-graph walk (`0x32072c`/`0x31f8e4`), `_ovg.bin` loading (formats `0x41/0x40/0x03/0x44/0x45/0x0b`, PackBits RLE `0x323dfc`), texture cache, FreeType text layout (VWThesis MQB + FZHT_GB18030), shader set (global alpha, `v_skin_color` tint), CxImage-based image processing, over EGL/GLES2 at 800×480. In QEMU this runs on the existing `tools/arm_gl/gl_fb_shim.c`; on device on Vivante.

### 5.6 IPC, input, tasks

Same threads/tasks as stock (`VControlTask`, `VDisplayTask`, `VmfThread`, `VCameraTask`, `VMLC*Task`), same Qt 4 event loop/signal-slot wiring, same VMF framing (26-byte header) and group subscriptions per `docs/hmi-internals.md` proxy table and `docs/ipc-and-vgw.md`. Input from tslib (`/dev/event1`), `/dev/event0` hardkeys and `/dev/tuner_knob` exactly as stock.

### 5.7 Build & deploy

- `tools/build_hmi.sh` (new, consistent with other `tools/build_*.sh`): fetch/prepare Qt 4.7.4 headers + host moc, run generators, cross-compile with zig + CMake, link against libraries taken from the local extracted rootfs / `build/cache/<fw>/stage` (sysroot).
- `tools/run_rcd330.sh` gains an option to stage `build/hmi/hmi` (and Skoda gresfiles) into the rootfs in place of stock `/opt/hmi`.

### 5.8 Verification

- **Host unit tests** for pure-data code (RLE decode, string block decode, scene table generation, VMF pack/unpack) — run on macOS without Qt/GL.
- **Parity in QEMU**: boot stock vs recreated HMI in `-M rcd330`, drive identical input sequences, compare framebuffer screenshots (pixel diff with tolerance) and VMF message traces per screen. Run under both PQ and MQB VRM/coding setups (simulated via the existing CAN/coding tooling).
- **Skoda look reference**: boot stock 5606 in QEMU and compare pages visually against the Skoda skin (look comparison, not pixel diff — layouts differ by design).
- **On-car**: only after QEMU parity of the touched area; recoverability first (stock `hmi` restorable via update package / `pwr_agent` fallback — builder to confirm mechanism before first flash).

### 5.9 Phasing

Incremental; each phase compiles and runs in QEMU.

0. **Toolchain & skeleton**: zig/CMake/Qt4 headers/moc; empty `hmi` that pwr_agent launches without breaking boot.
1. **IGDT + data**: generators, texture/string/scene loading, render one static stock screen pixel-matching stock.
2. **Framework**: tasks, `VBaseProxy` + VMF, notify filters, `PageManager`/`BasePage`, `controlcreator` + controls, touch/key/knob input. Home/start screen interactive.
3. **Pages by area** (one area per increment, with its proxy): tuner → media → phone/BT → sound → system setup (incl. language) → car (BAP) → popups → service/ETM/EOL → appconnect/CarPlay/AA/CarLife/MirrorLink → camera.
4. **Skoda skin** (asset/color overlay on 5624 layouts) + language table / `de_DE`.
5. **Device hardening**: real Vivante/tslib, watchdog/recovery, first flash.

## 6. Decided by the user

- Approach A (structural port), parity first, reference value, drop-in, eventual flashing.
- Code in `src/hmi`; one `src/<binary>/` per any other remade binary.
- VW + Skoda skins; all languages present in `resources/translations`.
- Skoda skin = 5624 layouts with Skoda look (not 5606 layouts); 5606 in QEMU is the look reference.
- Target rootfs: 5624 only.
- `de_DE` as a 6th language inside the HMI; language count should grow as far as possible for future community translations.

## 7. Calls made by Claude (correctable)

- Build system: CMake + zig toolchain file, wrapped by `tools/build_hmi.sh`.
- Data compiled in as generated C++ tables (mirrors stock `.rodata`), generated at build time from the local stock binaries, not loaded from JSON at runtime.
- Skin from VRM brand byte with dev override; Skoda gresfiles shipped in a separate directory; Skoda asset map generated by name/role matching + manual overrides.
- Language table data-driven from `resources/translations/`; outward codes restricted to stock-valid values.
- Parity tested under both PQ and MQB setups.
- Stock bugs reproduced and tagged, not fixed, during parity work.
- Host tests for pure-data code; QEMU screenshot + VMF-trace diff for parity.

## 8. Left to the builder

Exact generator formats, Qt header sourcing (vendored vs fetched), dev skin-override mechanism, Skoda gresfiles path, test harness choice, per-page ordering within phase 3, screenshot-diff tolerance.
