# src/hmi: recreated `opt/hmi` (firmware 5624)

C++ source that recreates the stock RCD 330 head-unit UI process `opt/hmi`
(VW firmware 5624, `sha256 44ccad42...a6f2`), function by function, from the
decompilation in `docs/decompiled/5624/hmi/`. It is built for the device
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

# Slow QEMU tests (several minutes): boot, mask-free pixel parity against
# stock, and the interactive suite (hardkeys, QMP touch, tune knob).
RCD330_QEMU_TESTS=1 python3 -m unittest discover -s tools/tests -p "test_hmi_*.py" -v

# Run the recreated hmi in the emulator. --qmp adds a QMP socket, which
# tools/hmi_qemu.py uses for touch input (input-send-event -> atmel_mxt).
tools/run_rcd330.sh --hmi build/hmi/hmi [--headless] [--monitor <sock>] [--qmp <sock>]

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
| `main.cpp` | `main` (0x10e58): stock startup order, then `QCoreApplication::exec()` |
| `control_logic/` | `VControlTask` (control thread), notify filters (`mode/`), key / knob drivers (`input/`) |
| `control_logic/subsystem/` | the `VBaseProxy` subsystem proxies (power, audio, display, AM/FM, media, BAP, PSS, App-Connect, diag, ...) and their portable decoders |
| `display/` | `VDisplayTask` (display thread), pages (`page/`), controls (`controls/`, `base/controlcreator`), touch (`multitouch/`) |
| `display/igdt/` | IGDT: tables, runtime state, string lookup, `_ovg.bin` decode, GLES2 renderer, scene walk, FreeType text, touch hit-test, `igdtTask` render thread |
| `service/` | libOSAL logging, libvmf_client (`VmfThread`, `VBaseProxy`), tslib, `VBaseTask` |
| `tools/` | `gen_igdt_tables.py`, `gen_strings.py` (tables from the stock binary), `gen_trace.py`, host Qt 4.7.4 `moc` build (`moc/`) |
| `tests/` | host C++ unit tests (`ctest`) |
| `TRACE.json` | generated index of recreated stock functions |

## Conventions

- **Origin tag** on the line above every recreated function:
  `// 0x<8 hex VA> <Name>` (5624 VA, lowercase hex). `gen_trace.py` only
  matches this form at the start of a line. Helpers that merely derive from a
  stock function say "helper derived from 0x..." and carry no tag.
- `// DEVIATION: <why>` where we intentionally differ from stock.
- `// STOCK-QUIRK: <what>` where a stock bug is reproduced on purpose.
- `// TODO(phase3): <what>` for behaviour deferred to the next phase (the
  other pages, popups, phone/BT, projection, camera).
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
(`docs/decompiled/5624/hmi/functions/0x<va>.c`) or lie in a non-code section
(`.rodata`, `.data`, `.bss`, ...) of `hmi_symbols_5624.json`; tags on data
addresses (tables, globals, shader strings) go under `_data` and do not count
toward coverage. An address inside a function body or a typo fails the check.
`gen_trace.py --check` (run by `test_hmi_build.py::TestHmiTrace`) fails when the
index is stale.

## Architecture (phase 2)

Threads, as in stock:

- **main**: `VControlTask::Init` creates every proxy singleton on the main
  thread, so their `QObject`s live there. `main` then starts the tasks, the
  drivers and `VmfThread` in stock order, and runs `exec()`.
- **VmfThread**: blocks in `nw_vmf_receive` and calls each registered
  proxy's `RecieveVmfMsg` with the listener mutex held. Proxies only update
  their own state there and hand work to the tasks as queued signals.
- **VControlTask**: the filter chain (`VIPCLNotifyFilter`,
  `VPowerNotifyFilter`, ...) for keys and power/audio notifications;
  receives everything through queued signals.
- **VDisplayTask**: every page lives on this thread. `VDisplayProxy` page
  requests (`IDisplaySink`), raw touches from `VMultiTouch` and IGDT touch
  hits from the render thread arrive as queued signals; a 20 ms timer drives
  the status line.
- **igdtTask** (render thread, stock `0x00062e60`): redraws on a timer.
  All IGDT state is behind one recursive mutex (`0x00a0fa40`), so touch
  callbacks may call the setters.
- **VKeyDriver / VKnobDriver / VMultiTouch**: blocking readers of
  `/dev/event0`, `/dev/tuner_knob` and tslib.

Hard keys are handled in two passes, as in stock. The driver key goes
through the filters, which update the `VHMIKey` state. The resulting key
event (press 3, short release 4, ...) goes to `VDisplayTask` and
`hmi_wrapper_igdt_post_button_event`. That function re-posts it to the
filters for the per-key handlers (`HandleLPCLHardKeyApp`, ...) and then
releases the held key (`0x000632bc` -> `0x00061244`).

Stock quirks and deviations are marked `STOCK-QUIRK:` / `DEVIATION:` where
they occur. [docs/hmi-internals.md](../../docs/hmi-internals.md) section 7
lists the ones that affect behaviour.

## Status

Phases 0-2 are done:

- **Phase 0-1:** zig toolchain, ABI gate, IGDT tables, strings, `_ovg.bin`,
  the GLES2 / FreeType renderer.
- **Phase 2:**
  - host `moc` emitting revision 62
  - `VBaseTask`, `VmfThread` / `VBaseProxy`, the subsystem proxies, the
    notify filters
  - key, knob and touch drivers; IGDT runtime and touch
  - controls and `ControlCreator`
  - `VPageManager`, `VDisplayTask`, and the boot pages: tuner shell `0xf9`,
    AppNoConn `0x04`, AppConnectInfo `0x03`
- **QEMU pixel parity is mask-free.** AppNoConn and AppConnectInfo are
  reached with real input, the same way as stock: APP key, then a QMP touch
  on Info. The whole 800x480 frame is compared, and at least 99.5 % of
  pixels must be within +-4 per channel. Current results:

  | Screen | Parity |
  |---|---|
  | AppNoConn | 99.84 % |
  | AppConnectInfo | 99.95 % |

  The rest of the AppNoConn difference is in the status line: stock shows
  the BAP outside temperature, and ours shows a mute icon there (phase 3
  status line).

  STOCK-QUIRK: the language is picked by a boot-time race. If diag's
  App-Connect coding is decoded before the PSS setup-block (0x4b) read
  fails, the region follows the coding and the language is en_US;
  otherwise it stays zh_CN. Ours ports that decision (`VPSSProxy` 0x4b
  reply, `VDisplayProxy::SetRegion`) and inherits the race. Ours starts
  earlier than stock, so it re-sends the read while pss answers "not ready"
  (status 10; DEVIATION, at most 10 x 200 ms). Nothing logs
  the outcome, so the parity test reads both languages from the pixels
  (ink width of the second welcome line on AppNoConn). It re-boots stock
  up to 3 times until stock matches ours, and skips with the race named
  if it never does. See [hmi-internals.md](../../docs/hmi-internals.md)
  §7, "Language".
- `tools/tests/test_hmi_interactive.py` covers:
  - boot, APP, touch Info, touch Back, RADIO
  - the tune knob
  - the phase-3 keys MEDIA and SETUP: each logs once, crashes nothing, and
    leaves APP and touch working
  - one hmi start (no `pwr_agent` restart)

Next: phase 3 pages by area (tuner, media, phone, sound, setup, car, popups,
service, projection, camera), phase 4 Skoda overlay, phase 5 device
hardening.
