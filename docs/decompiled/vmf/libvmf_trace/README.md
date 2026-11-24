[← Decompiled Firmware Index](../../README.md)

# `opt/lib/libvmf_trace.so` — VMF Trace & Diagnostic Logging Library (`5624_vw`)

- **Target binary**: `opt/lib/libvmf_trace.so` (`27,180` bytes, ARMv7-A shared library, SHA-256 `1c84e719259b60a651a9e1f8c0374dda65c9317a6ece15636d9ced738b365bbf`)
- **Decompiled**: `119` written / `151` total / `32` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Related**: [vmf_daemon/README.md](../vmf_daemon/README.md) · [libvmf_client/README.md](../libvmf_client/README.md)

## Overview

Optional structured tracing library companion to `libvmf_client.so`. Allows VMF services to register trace channels, filter log records by severity, emit formatted text or hex-dump trace packets to the VMF trace collector, and adjust trace control settings dynamically.

## Key Exported Functions

- [`0x114e8`](functions/0x114e8.c) — `vmf_trace_register`: registers a client module with the VMF trace subsystem.
- [`0x11f58`](functions/0x11f58.c) / [`0x1248c`](functions/0x1248c.c) / [`0x11ad4`](functions/0x11ad4.c) — `vmf_trace`, `vmf_trace_printf`, `vmf_trace_hex`: formatted text and raw hex-dump trace emitters.
- [`0x127e8`](functions/0x127e8.c) / [`0x129c4`](functions/0x129c4.c) / [`0x12f2c`](functions/0x12f2c.c) — `vmf_trace_send_ctrl`, `vmf_trace_set_severity`, `vmf_trace_check_filter`: runtime severity filtering and control commands.
- [`0x13fbc`](functions/0x13fbc.c) — `vmf_trace_get_time_in_ms`: monotonic timestamp helper.
