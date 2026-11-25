[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/libmlc_iface/README.md)

# `opt/lib/mirrorlink/libmlc_iface.so` — AllGo MirrorLink Client `7.5.7` (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/lib/mirrorlink/libmlc_iface.so` (`1,307,116` B vs. `5624` `1,266,543` B, `.text +24,984` B, ARMv7-A ELF)
- **Decompiled**: `1,998` written / `2,235` total / `237` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#15-optlibmirrorlinklibmlc_ifaceso-24984-b-text)

## Behavioral Differences vs. `5624_vw`

- Upgraded from **`AllGoMLC:1.1:7.5.3`** (`5624`) to **`AllGoMLC:1.1:7.5.7`** (`780`).
- Adds 6 new exports: `ales_osal_http_download_file_full`, `process_downloaded_data`, `devmgr_timer_callback_func`, `mlc_devmgr_send_wfd_device_available`, `mlc_wfd_discover_devices`, and `mlc_wfd_listen_devices`.
- Enhances RTP silence gating (`audmgr_rtp_client_thread_func`, +62 lines), XML app-list/certified/notification parsing (`devmgr_get_app_list_from_device`, +101 lines), and connection step-timing trace logs.
