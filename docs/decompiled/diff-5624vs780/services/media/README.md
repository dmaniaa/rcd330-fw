[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/media/README.md)

# `opt/media` — Media Playback & iPod Service (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/media` (`384,008` B vs. `5624` `384,008` B, `.text +516` B, ARMv7-A ELF)
- **Decompiled**: `1,157` written / `1,305` total / `148` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#5-optbt-2064-b-text--6-optmedia-516-b-text)

## Behavioral Differences vs. `5624_vw`

- Replaces `media_app_vmf_message_send_to_spp` with `media_app_send_msg_to_BT` ([`0x2dac0`](functions/0x2dac0.c)) → `media_app_vmf_message_send_to_BT` ([`0x3bb2c`](functions/0x3bb2c.c)).
- Widens `media_app_spt_send_msg_build` ([`0x162ac`](functions/0x162ac.c), `< 0x37 → < 0x38`) to send connected USB iPod name (`0x37`) and removal (`0x17`) to `opt/bt` over VMF `(0x24, 0x02)` for USB-iPod/BT-A2DP conflict arbitration.
