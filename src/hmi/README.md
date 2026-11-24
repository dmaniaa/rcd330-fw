# src/hmi: recreated `opt/hmi` (firmware 5624)

C++ source that recreates the stock RCD 330 head-unit UI process `opt/hmi`
(VW firmware 5624, `sha256 44ccad42...a6f2`), function by function, from the
decompilation in `docs/decompiled/hmi/`. It is built for the device
(ARMv7 softfp, glibc 2.13, GCC 4.6 libstdc++, Qt 4.7.4 `libQtCore` only) and
runs under the QEMU `-M rcd330` emulator in place of the stock binary.

## Build, run, test

```sh
# Cross-build build/hmi/hmi with zig and run the ABI gate (needs zig 0.17.1
# exactly -- pinned in cmake/zig-armv7-toolchain.cmake because the glibc 2.13
# header workarounds depend on zig's bundled headers -- plus cmake, ninja,
# curl; first run downloads pinned third-party headers). The binary is gated
# as build/hmi/hmi.new and only moved to build/hmi/hmi when the gate passes.
# Also (re)creates src/hmi/compile_commands.json (gitignored) for editors.
tools/build_hmi.sh [--force]

# Host unit tests (host compiler, no zig). Includes the _ovg.bin sweep over the
# stock gresfiles, so it needs a prior full build (build/hmi/gen/) and
# extracted-fw/5624_vw/opt/gresfiles (override with HMI_GRESFILES); it fails
# if either is missing. --no-assets skips the sweep and needs no firmware.
tools/build_hmi.sh --host-tests [--no-assets]

# Python gates: ABI/DT_NEEDED/Qt-import checks, tables, trace index.
python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v

# Slow QEMU boot and pixel-parity tests (several minutes).
RCD330_QEMU_TESTS=1 python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v

# Run the recreated hmi in the emulator; HMI_DEV_SCREEN draws one IGDT screen.
tools/run_rcd330.sh --hmi build/hmi/hmi [--headless] [--profile-env HMI_DEV_SCREEN=0x04]

# Regenerate the stock-VA -> source index after adding or moving origin tags.
python3 src/hmi/tools/gen_trace.py
```

Needs the staged firmware: `tools/build_rootfs.sh <5624 firmware .tar>` and
`extracted-fw/5624_vw/opt/hmi`. `tools/build_hmi.sh` stops with the missing
path and the command that produces it. Generated tables go to `build/hmi/gen/`
and are never committed.

## Layout

Source paths and class names follow the recovered stock paths
(`main.cpp`, `display/igdt/extend/igdt_wrapper.cpp`, ...). A file whose stock
name is unknown starts with `// File name is ours; stock name unknown.`

| Path | Contents |
|---|---|
| `main.cpp` | `main` (0x10e58) skeleton: `QCoreApplication::exec()` |
| `display/igdt/` | IGDT: string lookup, `_ovg.bin` decode, GLES2 renderer, scene walk, FreeType text |
| `service/osal/` | libOSAL logging shim |
| `tools/` | `gen_igdt_tables.py`, `gen_strings.py` (tables from the stock binary), `gen_trace.py` |
| `tests/` | host C++ unit tests (`ctest`) |
| `TRACE.json` | generated index of recreated stock functions |

## Conventions

- **Origin tag** on the line above every recreated function:
  `// 0x<8 hex VA> <Name>` (5624 VA, lowercase hex). `gen_trace.py` only
  matches this form at the start of a line. Helpers that merely derive from a
  stock function say "helper derived from 0x..." and carry no tag.
- `// DEVIATION: <why>` where we intentionally differ from stock.
- `// STOCK-QUIRK: <what>` where a stock bug is reproduced on purpose.
- `// TODO(phase2): <what>` for behaviour deferred to the next phase
  (page/widget-driven state).
- Each file ends with a newline. Qt is imported only through symbols stock
  `opt/hmi` also imports (the device runs Qt Embedded 4.7.4; the sysroot
  `libQtCore` is a stand-in), and `DT_NEEDED` must stay a subset of stock's.
  Both are enforced by `tools/tests/test_hmi_build.py`, as is the glibc 2.13 /
  `GLIBCXX_3.4.16` / `CXXABI_1.3.5` symbol-version ceiling.

## Traceability index (`TRACE.json`)

`gen_trace.py` scans `src/hmi/**/*.{cpp,h}` for origin tags and writes
`TRACE.json`:

```
"0x0005ff4c": {"name": ..., "file": ..., "line": ...}   one entry per function
"_data":      {...}                                      tags on data addresses
"_coverage":  {"recreated": N, "total": 9580}
```

Every tagged VA must be a decompiled function
(`docs/decompiled/hmi/functions/0x<va>.c`) or lie in a non-code section
(`.rodata`, `.data`, `.bss`, ...) of `hmi_symbols_5624.json`; tags on data
addresses (tables, globals, shader strings) go under `_data` and do not count
toward coverage. An address inside a function body or a typo fails the check.
`gen_trace.py --check` (run by `test_hmi_build.py::TestHmiTrace`) fails when the
index is stale.

## Status

Phase 0-1 (toolchain, IGDT) is done:

- zig cross toolchain, ABI gate, boot skeleton that stays in `exec()`
- IGDT tables (`tex_table`, `scr_table`, text layouts, scenes) generated from
  the stock binary; localized string block decoder
- `_ovg.bin` RLE/pixel-format decode matching stock 0x325728
- GLES2 image rendering and scene walk, FreeType text rendering
- AppNoConn (screen 0x04) is pixel-compared with stock in QEMU
  (>= 99.5 % of unmasked pixels within +-4 per channel; about 99.9 % reached)

Parity masks follow the controller rulings: image boxes of dynamic records,
the status line (clock, temperature, status icons) and the Info button glow
are masked because they depend on page/widget state that does not exist yet.
Phase 2 must produce that state (VBaseProxy/VMF, notify filters, PageManager,
controls) and then **remove these masks**, so the dynamic-record and
widget-state images are compared too.

Next: phase 2 (host-built Qt `moc`, tasks/threads, proxies, pages and
controls), phase 3 pages, phase 4 Skoda overlay, phase 5 device hardening.
