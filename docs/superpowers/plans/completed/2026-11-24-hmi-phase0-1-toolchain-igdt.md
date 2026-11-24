# HMI Recreation — Phase 0–1 (Toolchain, Skeleton, IGDT Static Screen) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A cross-compiled `src/hmi` binary that drops into the stock 5624 rootfs in place of `/opt/hmi`, boots under `pwr_agent` without breaking the system, and renders the stock `AppNoConn` screen (`eScrId 0x04`, static images + static text) pixel-matching stock in QEMU.

**Architecture:** Structural port of stock `opt/hmi` (spec §4 approach A). This plan covers spec phases 0 and 1 only: build toolchain against the device ABI, process skeleton mirroring stock `main` (`0x10e58`), build-time table generators that read the stock `.rodata` tables from the local `extracted-fw/5624_vw/opt/hmi`, and the first slice of the IGDT reimplementation (`_ovg.bin` decode, GLES2 image draw, FreeType text). Phases 2–5 get their own plans.

**Tech Stack:** C++98 (GCC 4.6 libstdc++ headers) compiled by `zig clang` for `armv7a-linux-gnueabi`, CMake, Qt 4.7.4 headers (QtCore only, no `moc` yet), EGL/GLES2, FreeType, Python 3 (generators + tests, `unittest` like `tools/tests/`), QEMU `-M rcd330`.

**Spec:** `docs/superpowers/specs/2026-11-24-hmi-recreation-design.md`

## Global Constraints

- Target: `armv7a-linux-gnueabi`, `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp`, glibc **2.13** symbol versions (as `tools/build_shims.sh`).
- C++ runtime: device `libstdc++.so.6.0.16` (GCC 4.6). Max symbol versions in the output: `GLIBCXX_3.4.16`, `CXXABI_1.3.5`, `GLIBC_2.13`. Never link zig's libc++.
- `DT_NEEDED` of `build/hmi/hmi` must be a subset of stock `opt/hmi`'s list (spec §3).
- Sysroot for linking = `build/cache/std_plus_pss-6.5inch_5624_VW_PQ_ROW/stage` (libs from `/lib`, `/usr/lib`, `/opt/lib`, `/usr/local/qte4.7.4/lib`).
- Source paths/class names follow the recovered stock paths (`main.cpp`, `display/igdt/extend/igdt_wrapper.cpp`, …). Files with unknown stock names get a header line `// File name is ours; stock name unknown.`
- Every recreated function has an origin tag on the line above it: `// 0x<8-hex VA> <Name>` (5624 VA). Deviations: `// DEVIATION: <why>`. Reproduced stock bugs: `// STOCK-QUIRK: <what>`.
- Nothing from `extracted-fw/`, `original-fw/`, `build/` is committed; generated tables go to `build/hmi/gen/`.
- Each file ends with a newline. Commit after each task (`--no-verify` forbidden).

## Review Focus

1. **Missing local firmware** (`extracted-fw/5624_vw/opt/hmi` or the staged rootfs absent, or sha256 ≠ `44ccad42…a6f2`) → `tools/build_hmi.sh` must stop with a message naming the missing path and the command that produces it (`tools/build_rootfs.sh`), not emit partial tables. Test in Task 3.
2. **HMI crash/exit on boot** → `pwr_agent` restart loop; the skeleton must stay in `QCoreApplication::exec()` and the boot test must assert exactly one start banner after 60 s. Test in Task 2.
3. **Missing/corrupt `_ovg.bin`** (file absent from `/opt/gresfiles`, or RLE stream shorter/longer than `w*h*bpp`) → log once with path and skip the element; never crash or read past the buffer. Test in Task 5.
4. **Text string ID with no entry for the active language** → fall back to `en_US` text, never draw garbage or crash. Test in Task 7.
5. **ABI drift** (a newer libstdc++/glibc symbol version sneaking in when code is added) → build-time check fails the build. Test in Task 1, rerun by every later task's build.

---

## File Structure

```
tools/build_hmi.sh                         # fetch third-party headers, generate tables, cmake+zig build → build/hmi/hmi
tools/hmi_qemu.py                          # headless QEMU harness: boot, VIP text cmds, screendump, serial capture
tools/tests/test_hmi_build.py              # ABI/NEEDED gate on build/hmi/hmi
tools/tests/test_hmi_tables.py             # generator oracle tests vs resources/
tools/tests/test_hmi_boot.py               # QEMU boot test (RCD330_QEMU_TESTS=1)
tools/tests/test_hmi_parity.py             # QEMU pixel parity vs stock (RCD330_QEMU_TESTS=1)
tools/build_rootfs.sh, tools/run_rcd330.sh # + --hmi <path>, run: + --headless --monitor <sock>
src/hmi/CMakeLists.txt
src/hmi/cmake/zig-armv7-toolchain.cmake
src/hmi/third_party/README.md              # what fetch provides (headers are fetched into build/, not committed)
src/hmi/third_party/qt/qconfig.h           # minimal Qt Embedded 4.7.4 config for QtCore headers (ours)
src/hmi/main.cpp                           # 0x10e58 main
src/hmi/service/osal/osal_api.h            # extern "C" decls for libOSAL log_simple/printlog, ini_getl (ours)
src/hmi/tools/hmi_elf.py                   # ELF PT_LOAD VA→offset reader for stock opt/hmi
src/hmi/tools/gen_igdt_tables.py           # scr/tex/node/text_layout tables → build/hmi/gen/igdt_tables_vw.cpp
src/hmi/tools/gen_strings.py               # string block → build/hmi/gen/strings_vw.cpp
src/hmi/display/igdt/igdt_tables.h         # C structs mirroring stock .rodata tables (ours)
src/hmi/display/igdt/igdt_ovg.{h,cpp}      # 0x325728 / 0x323dfc texture entry + RLE decode (ours: file name)
src/hmi/display/igdt/igdt_gl.{h,cpp}       # EGL/GLES2 context, shaders, texture cache, quad draw (ours)
src/hmi/display/igdt/igdt_scene.{h,cpp}    # 0x32072c / 0x31f8e4 scene walk + draw (ours)
src/hmi/display/igdt/igdt_text.{h,cpp}     # FreeType font table + text layout/draw (ours)
src/hmi/display/igdt/extend/igdt_wrapper.{h,cpp}  # stock-named wrapper entry points used by pages later
src/hmi/tests/CMakeLists.txt, tests/test_igdt_*.cpp   # host-built (clang, macOS) unit tests
src/hmi/TRACE.json                         # generated VA → file:line index
src/hmi/tools/gen_trace.py
```

---

### Task 1: Cross toolchain and ABI gate

**Files:**
- Create: `tools/build_hmi.sh`, `src/hmi/CMakeLists.txt`, `src/hmi/cmake/zig-armv7-toolchain.cmake`, `src/hmi/third_party/README.md`, `src/hmi/third_party/qt/qconfig.h`, `src/hmi/main.cpp` (probe version), `tools/tests/test_hmi_build.py`

**Interfaces:**
- Produces: `tools/build_hmi.sh [--force] [--host-tests]` → `build/hmi/hmi`; CMake toolchain file usable by later tasks; fetched headers under `build/hmi/third_party/{gcc46,qt474}/`.

- [ ] **Step 1: Write failing test** `tools/tests/test_hmi_build.py` (`unittest`; skip when `build/hmi/hmi` is missing unless `RCD330_REQUIRE_HMI=1`):
  - `test_is_arm_elf_exec`: `llvm-readelf -h -l` → `Machine: ARM`, `Type: EXEC`, interpreter `/lib/ld-linux.so.3`.
  - `test_needed_subset_of_stock`: `DT_NEEDED(build/hmi/hmi) ⊆ DT_NEEDED(extracted-fw/5624_vw/opt/hmi)`.
  - `test_symbol_versions_within_device`: from `llvm-readelf -V`, every `GLIBCXX_x` ≤ `3.4.16`, `CXXABI_x` ≤ `1.3.5`, `GLIBC_x` ≤ `2.13`; no `__1::` (libc++) symbols.
  - `test_float_abi_softfp`: `Tag_ABI_VFP_args` absent in `llvm-readelf -A`.
- [ ] **Step 2: Run** `RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v` → FAIL (binary missing).
- [ ] **Step 3: Third-party fetch in `tools/build_hmi.sh`** into `build/hmi/third_party/`, each pinned by sha256 (record the hash verified on first download in the script):
  - GCC 4.6 libstdc++ headers: Debian armel `libstdc++6-4.6-dev_4.6.3-14_armel.deb` (archive.debian.org, pool `main/g/gcc-4.6/`); extract `usr/include/c++/4.6` and `usr/include/arm-linux-gnueabi/c++/4.6` with `ar` + `tar`. If unavailable, use another GCC 4.6.x armel libstdc++ dev package and note which.
  - Qt 4.7.4: `https://download.qt.io/archive/qt/4.7/qt-everywhere-opensource-src-4.7.4.tar.gz`; extract `include/QtCore/` and the `src/corelib/` headers it forwards to.
  - Fail fast naming the missing path if the sysroot or `extracted-fw/5624_vw/opt/hmi` is absent (Review Focus 1).
- [ ] **Step 4: Toolchain file** `zig-armv7-toolchain.cmake`: C via `zig cc -target arm-linux-gnueabi.2.13`; C++ compile via `zig clang -target armv7a-linux-gnueabi -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp -std=gnu++98 -nostdinc++ -isystem <gcc46>/c++/4.6 -isystem <gcc46>/arm-linux-gnueabi/c++/4.6 -D__GLIBC_MINOR__=13 -U_FILE_OFFSET_BITS -U_TIME_BITS`; link via `zig cc -target arm-linux-gnueabi.2.13` (C driver → no libc++) plus explicit `<sysroot>/lib/libstdc++.so.6`, `<sysroot>/lib/libgcc_s.so.1`, `-Wl,-rpath-link` to all sysroot lib dirs, dynamic linker `/lib/ld-linux.so.3`.
- [ ] **Step 5: `qconfig.h`**: minimal Qt Embedded 4.7.4 QtCore config (`QT_EDITION_OPENSOURCE`, `Q_WS_QWS`, little-endian ARM; `QT_NO_*` only as needed to compile). Correctness proven by Step 6 linking.
- [ ] **Step 6: Probe `main.cpp`**: uses `std::string`, `std::map`, `std::vector`, a thrown+caught exception, `QCoreApplication`, `QString`, `QTextCodec::codecForName("utf8")`, `QTimer::singleShot`, `QThread::currentThread()`; links against device `libQtCore.so.4` with unresolved symbols treated as errors (unresolved Qt symbol = qconfig/ABI mismatch → fix qconfig, never stub).
- [ ] **Step 7: Run** `tools/build_hmi.sh && RCD330_REQUIRE_HMI=1 python3 -m unittest tools/tests/test_hmi_build.py -v` → 4 PASS. If clang cannot use the GCC 4.6 headers after 3 attempts, stop and report.
- [ ] **Step 8: Commit** `build: add zig cross toolchain and ABI gate for src/hmi`.

### Task 2: Stock-shaped skeleton that boots under pwr_agent

**Files:**
- Modify: `src/hmi/main.cpp` (replace probe), `tools/build_rootfs.sh` (`--hmi <path>`), `tools/run_rcd330.sh` (`--hmi <path>`, `--headless`, `--monitor <unix-socket>`)
- Create: `src/hmi/service/osal/osal_api.h`, `tools/hmi_qemu.py`, `tools/tests/test_hmi_boot.py`

**Interfaces:**
- Consumes: Task 1 build.
- Produces: `tools/hmi_qemu.py`: `class Rcd330Qemu(hmi: str | None = None, env: dict | None = None)` context manager (`hmi=None` → stock; `env` → extra exports appended to staged `/etc/profile`); methods `wait_serial(pattern: str, timeout: float) -> str`, `serial_text() -> str`, `vip(cmd: str) -> None` (one text line to the VIP TCP port, e.g. `"BTN APP 1"`, protocol in `qemu/hw/misc/rcd330_vip.c` `vip_process_cmd`), `screendump(path: str) -> None` (QEMU monitor `screendump`, PPM), `close()`.

- [ ] **Step 1: Failing test** `tools/tests/test_hmi_boot.py` (skipped unless `RCD330_QEMU_TESTS=1`):
  - `test_recreated_hmi_boots_and_stays_up`: `Rcd330Qemu(hmi="build/hmi/hmi")`; `wait_serial(r"\[HMI_START\]HMI main start", 180)`; wait 60 s; banner count == 1; no `Segmentation fault`.
  - `test_stock_hmi_still_boots`: same with `hmi=None`.
- [ ] **Step 2: Run** `RCD330_QEMU_TESTS=1 python3 -m unittest tools/tests/test_hmi_boot.py -v` → FAIL.
- [ ] **Step 3: Rootfs/run options**: `build_rootfs.sh --hmi <path>` copies the file over `$STAGE_DIR/opt/hmi` (mode 755) before cpio and writes a separate cpio (`rootfs_hmi.cpio.gz`) so the stock cache stays intact; `--profile-env KEY=VAL` (repeatable) appends to `/etc/profile`. `run_rcd330.sh --headless` skips the GUI; `--monitor <sock>` adds `-monitor unix:<sock>,server,nowait`; `--hmi` forwards to `build_rootfs.sh`.
- [ ] **Step 4: `main.cpp`** — `// 0x00010e58 main`: replicate the stock prologue in order: banner via `log_simple` + `printlog(3, …, "main.cpp", …, 0x73, …)` with text `" ==== HMI ==== INFO: [HMI_START]HMI main start\n"`; `ini_getl("AUTOTEST","ENABLE",0,"/pss1/hmi_autotest.ini")`; `ini_getl("CODING","CODINGS",-1,"/pss1/stdplus.ini") >> 1` RVC check with the same log lines; `QCoreApplication`; utf8 codec set for tr / C strings / locale; `app.exec()`. Remaining stock calls become ordered placeholders `// TODO(phase2): 0x000de600 …` so phase 2 fills them 1:1. Declare libOSAL functions in `osal_api.h` from the actual exports of `extracted-fw/5624_vw/opt/lib/libOSAL.so.1` and `docs/decompiled/libOSAL`. If the libOSAL log doesn't reach the serial console, also write the banner to stderr (`// DEVIATION: test visibility`).
- [ ] **Step 5: Run** boot tests → 2 PASS.
- [ ] **Step 6: Commit** `hmi: stock-shaped main skeleton; --hmi rootfs staging and headless QEMU harness`.

### Task 3: IGDT table generator from stock `.rodata`

**Files:**
- Create: `src/hmi/tools/hmi_elf.py`, `src/hmi/tools/gen_igdt_tables.py`, `src/hmi/display/igdt/igdt_tables.h`, `tools/tests/test_hmi_tables.py`
- Modify: `tools/build_hmi.sh`, `src/hmi/CMakeLists.txt` (compile `build/hmi/gen/igdt_tables_vw.cpp`)

**Interfaces:**
- Produces (Python): `hmi_elf.Elf(path)` with `.read(va, n) -> bytes`, `.u8/.u16/.u32/.cstr(va)`; VA→offset per `AGENTS.md` (`PT_LOAD[0]`: `-0x8000`, `PT_LOAD[1]`: `-0x10000`). `gen_igdt_tables.load_screens(elf) -> dict[int, dict]` whose values have exactly the keys/values of `resources/scenes/scenes_5624_vw.json` screens (`screen_id, name, layer_group, root_count, element_count, elements`; elements: `type, tag, x, y, w, h, alpha, tint, asset` for img, `font, align, color` for txt). `load_textures(elf) -> list[tuple]` (24-byte `<BBHhhIHHII>`, `docs/assets-and-localization.md` §1).
- Produces (C++), `igdt_tables.h`:
  `struct IgdtTexEntry { uint8_t fmt, flags; uint16_t a0; int16_t a1, a2; uint32_t bsz; uint16_t w, h; uint32_t tint; const char* path; };`
  `struct IgdtElement { uint8_t type; uint32_t tag; int16_t x, y, w, h; uint8_t alpha; uint32_t color; int16_t tex; uint8_t font; uint8_t align; uint32_t str_id; };` (`color` = tint for img, text color for txt)
  `struct IgdtScreen { uint16_t id; const char* name; uint8_t layer_group; uint16_t elem_first, elem_count; };`
  `struct IgdtSkinTables { const IgdtScreen* screens; uint16_t screen_count; const IgdtElement* elements; const IgdtTexEntry* textures; uint16_t tex_count; };`
  `extern const IgdtSkinTables g_igdtTablesVw;` — elements flattened in scene-walk draw order. Add fields the walker needs (child counts, `branch_init`/`state_init`) if `0x32072c` requires them, named after the `docs/hmi-internals.md` §6 table.

- [ ] **Step 1: Failing tests** `tools/tests/test_hmi_tables.py`:
  - `test_screens_match_extracted_json`: for each of the 14 ids in `scenes_5624_vw.json`, `load_screens()[id]` == the JSON screen (minus `widget_boxes`, which is page-side data).
  - `test_screen_count_255`: 255 entries from `scr_table` (`0x4536a4`, 16 B).
  - `test_texture_dedup_2806`: 2,806 unique `path`s in `tex_table` (`0x69b69c`); each exists in `extracted-fw/5624_vw/opt/gresfiles`.
  - `test_missing_firmware_fails_fast`: `gen_igdt_tables.py --hmi /nonexistent` → non-zero exit; stderr names `/nonexistent` and `tools/build_rootfs.sh`.
  - `test_hmi_sha256_checked`: wrong file → non-zero exit naming expected `44ccad4211fc8f2fb21f2db46a04271155b8d894735427802baee7467630a6f2`.
- [ ] **Step 2: Run** `python3 -m unittest tools/tests/test_hmi_tables.py -v` → FAIL.
- [ ] **Step 3: Implement** from `docs/hmi-internals.md` §6 (VAs/strides) and walkers `docs/decompiled/hmi/functions/0x32072c.c`, `0x31f8e4.c`; texture entries per `0x325728.c`. Emit `build/hmi/gen/igdt_tables_vw.cpp` for all 255 screens; call it from `build_hmi.sh` before cmake.
- [ ] **Step 4: Run** → 5 PASS; Task 1 tests still pass after `tools/build_hmi.sh`.
- [ ] **Step 5: Commit** `hmi: generate IGDT scene/texture tables from stock .rodata`.

### Task 4: String block generator

**Files:**
- Create: `src/hmi/tools/gen_strings.py`, `src/hmi/display/igdt/igdt_strings.h` (ours), `src/hmi/tests/test_igdt_strings.cpp`
- Modify: `tools/tests/test_hmi_tables.py`, `src/hmi/tools/gen_igdt_tables.py` (fill `str_id`), `tools/build_hmi.sh`, `src/hmi/CMakeLists.txt`, `src/hmi/tests/CMakeLists.txt`

**Interfaces:**
- Produces (Python): `gen_strings.decode_block(elf) -> dict`, same shape as `resources/translations/strings_5624_vw.json`.
- Produces (C++): `const char* IgdtGetString(uint32_t str_id, int lang)` → UTF-8; `lang` ∈ stock order `{0 en_US, 1 zh_CN, 2 pt_BR, 3 ru_RU, 4 es_MX}` (6-language table is phase 4); empty translation → `en_US` text; unknown id → `""`.

- [ ] **Step 1: Failing tests**: `test_string_block_matches_json` (decoded == `strings_5624_vw.json`: 222 neutral, 1,976 multilang); `test_block_location` (file `0x3c3680`, size `0x20fcb`, multilang start `0x02d2`); host `test_igdt_strings.cpp`: id with empty `zh_CN` → `en_US` text; unknown id → `""` (Review Focus 4).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** per `0x23650.c` + `docs/assets-and-localization.md` §2 → `build/hmi/gen/strings_vw.cpp`; derive each txt element's `str_id` from the `static_txt` node (`0x3c` B) and fill it in the Task 3 generator.
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `hmi: generate localized string tables from stock string block`.

### Task 5: `_ovg.bin` decode with host unit tests

**Files:**
- Create: `src/hmi/display/igdt/igdt_ovg.h`, `src/hmi/display/igdt/igdt_ovg.cpp`, `src/hmi/tests/CMakeLists.txt`, `src/hmi/tests/test_igdt_ovg.cpp`
- Modify: `tools/build_hmi.sh` (`--host-tests`: build `src/hmi/tests` with host clang, run ctest)

**Interfaces:**
- Produces: `bool IgdtDecodeOvgRle(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, int bpp)` (`// 0x00323dfc`), false on under/overrun; `bool IgdtLoadOvg(const IgdtTexEntry& e, const char* root, std::vector<uint8_t>& rgba_out)` (`// 0x00325728`) → RGBA8888 `w*h*4`; false + one log line with the path on missing/corrupt file. Formats: `0x41`/`0x40` RGBA8888, `0x03` RGB565, `0x44` RGBA4444, `0x45` RGBA5551, `0x0b` A8 (→ white RGB, alpha = value; tint at draw time).

- [ ] **Step 1: Failing tests** `test_igdt_ovg.cpp` (minimal assert runner, no framework):
  - RLE: `{0x82, 0xAA}` bpp1 → `AA AA AA`; `{0x01, 0x10, 0x20}` bpp1 → `10 20`; mixed run+literal bpp2; truncated input → false; output overflow → false with no write past `dst_len`.
  - One pixel per format → RGBA: RGB565 `0xF800` → `FF 00 00 FF`; RGBA4444 `0xF00F` → `FF 00 00 FF`; RGBA5551 per bit layout in `0x325728.c`; A8 `0x80` → `FF FF FF 80`.
  - Missing file → false, no crash (Review Focus 3).
  - `real_gresfiles_sweep` (only if `HMI_GRESFILES` set): all 2,806 unique entries decode to exactly `w*h*bpp`, consuming exactly `bsz` bytes.
- [ ] **Step 2: Run** `tools/build_hmi.sh --host-tests` → FAIL.
- [ ] **Step 3: Implement** from `0x323dfc.c` / `0x325728.c` (bit layouts from decompiled code).
- [ ] **Step 4: Run** `HMI_GRESFILES=$PWD/extracted-fw/5624_vw/opt/gresfiles tools/build_hmi.sh --host-tests` → PASS.
- [ ] **Step 5: Commit** `hmi: IGDT _ovg.bin RLE and pixel-format decode with host tests`.

### Task 6: GLES2 image rendering of a scene

**Files:**
- Create: `src/hmi/display/igdt/igdt_gl.{h,cpp}`, `src/hmi/display/igdt/igdt_scene.{h,cpp}`, `src/hmi/display/igdt/extend/igdt_wrapper.{h,cpp}`, `tools/tests/test_hmi_parity.py`
- Modify: `src/hmi/main.cpp`, `src/hmi/CMakeLists.txt` (link `libEGL.so.1`, `libGLESv2.so.2`, `libGAL.so`)

**Interfaces:**
- Consumes: `g_igdtTablesVw`, `IgdtLoadOvg`, `Rcd330Qemu`.
- Produces: `bool IgdtGlInit(int w, int h)` (same `fb*`/`egl*` call sequence as stock — the 4 `fb*` + 11 `egl*` calls listed in `tools/arm_gl/gl_fb_shim.c`); `GLuint IgdtTexture(int tex_index)` (cached); `void IgdtDrawImage(const IgdtElement& e, GLuint tex)`; `void IgdtSceneDraw(const IgdtSkinTables& t, uint16_t screen_id)` (`// 0x0032072c`); `void IgdtSwap()`. `igdt_wrapper` holds the stock-named `hmi_wrapper_igdt_*` entry points needed so far (names from `docs/decompiled/hmi`). Dev hook: env `HMI_DEV_SCREEN=0x04` → `main` draws that screen once after init (`// DEVIATION: dev-only until phase 2 PageManager`).

- [ ] **Step 1: Failing test** `test_hmi_parity.py::test_appnoconn_images` (`RCD330_QEMU_TESTS=1`): stock boot → wait for HMI ready → `vip("BTN APP 1")`, `vip("BTN APP 0")` → wait 3 s → `screendump(stock.ppm)`; `Rcd330Qemu(hmi="build/hmi/hmi", env={"HMI_DEV_SCREEN": "0x04"})` → `screendump(ours.ppm)`. Mask = all `txt` element boxes of screen `0x04` + status-line dynamic boxes. Assert ≥ 99.5 % of unmasked pixels within ±4 per channel; on failure save both PPMs + diff PNG to `build/hmi/parity/`.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.** GLSL copied verbatim from stock `.rodata` (shader setup referencing `v_skin_color`, `docs/hmi-internals.md` §1); same attribute/uniform names; blending and element `alpha`/`tint` uniforms as stock; texture root `/opt/gresfiles/`.
- [ ] **Step 4: Run** → PASS (use the diff PNG; stop after 3 failed hypotheses on one mismatch and report).
- [ ] **Step 5: Commit** `hmi: IGDT GLES2 image rendering and scene walk; AppNoConn image parity`.

### Task 7: FreeType text rendering

**Files:**
- Create: `src/hmi/display/igdt/igdt_text.{h,cpp}`
- Modify: `src/hmi/display/igdt/igdt_scene.cpp`, `src/hmi/CMakeLists.txt` (link `libfreetype.so.6`), `tools/tests/test_hmi_parity.py`

**Interfaces:**
- Consumes: `IgdtGetString`, `IgdtElement.font/align/color/str_id`.
- Produces: `bool IgdtTextInit(const char* font_root)`; `void IgdtDrawText(const IgdtElement& e, const char* utf8)`. Font table (index → ttf, pixel size, style), `align` bit meanings (`0x20`, `0x22`, …) and `text_layout` (`0x46d6a0`, `0x14` B) taken from stock code (callers of `FT_New_Face` / `FT_Set_Pixel_Sizes` in `docs/decompiled/hmi`); fonts `VWThesis MQB Regular.ttf`, `VWThesis MQB Light.ttf`, `FZHT_GB18030.ttf` from `/opt/gresfiles`, CJK fallback as stock.

- [ ] **Step 1: Failing test** `test_hmi_parity.py::test_appnoconn_full`: as Task 6 but mask only dynamic boxes (status-line clock `0x05100000`, `dyn_txt` elements); ≥ 99.5 % within ±4.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** per stock text code (A8 glyph textures + `v_skin_color` tint, antialiasing as stock).
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `hmi: IGDT FreeType text rendering; AppNoConn full parity`.

### Task 8: Traceability index and docs

**Files:**
- Create: `src/hmi/tools/gen_trace.py`, `src/hmi/TRACE.json`, `src/hmi/README.md`
- Modify: `AGENTS.md` (repo map `src/` row; `tools/build_hmi.sh` in §3 workflows), `docs/hmi-internals.md` (newly verified facts: font table, shader names, node fields), `tools/tests/test_hmi_build.py`

**Interfaces:**
- Produces: `gen_trace.py [--check]` scans `src/hmi/**/*.{cpp,h}` for `// 0x[0-9a-f]{8} <name>` → `TRACE.json` `{"0x0005ff4c": {"name", "file", "line"}, …, "_coverage": {"recreated": N, "total": 9580}}`.

- [ ] **Step 1: Failing test** `test_hmi_build.py::test_trace_index_current`: `gen_trace.py --check` exits 0, and every tagged VA has `docs/decompiled/hmi/functions/0x<va>.c` (VA normalized to the file naming).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement**; generate `TRACE.json`; `README.md`: build/run/test commands, conventions (origin tag, `DEVIATION`, `STOCK-QUIRK`, file-name note), phase status.
- [ ] **Step 4: Run** `tools/build_hmi.sh --host-tests && python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v`, then again with `RCD330_QEMU_TESTS=1` → all PASS.
- [ ] **Step 5: Commit** `hmi: traceability index, README, docs sync`.

---

## Not in this plan (next plans)

- Phase 2: host-built Qt 4.7.4 `moc`, tasks/threads, `VBaseProxy` + VMF, notify filters, `PageManager`/`BasePage`, `controlcreator`, controls, input drivers.
- Phase 3: pages per area. Phase 4: Skoda overlay + data-driven language table (incl. outward-code safety audit). Phase 5: device hardening / first flash.
