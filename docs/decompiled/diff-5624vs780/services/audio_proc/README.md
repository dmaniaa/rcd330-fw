[← RCD 780 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/audio_proc/README.md)

# `opt/audio_proc` — Audio Manager & ALSA/DSP Router (RCD 780 / `8.5.9.7` CR222)

- **Target binary**: `opt/audio_proc` (`168,940` B vs. `5624` `168,636` B, `.text +284` B, ARMv7-A ELF)
- **Decompiled**: `698` written / `838` total / `140` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-rcd780.md](../../../../diff-5624-vs-rcd780.md#7-optaudio_proc-284-b-text)

## Behavioral Differences vs. `5624_vw`

- Adds MirrorLink ALSA/DSP teardown synchronization: `audio_vaf_mlink_thread` ([`0xb58c`](functions/0xb58c.c)) notifies [`0x235dc`](functions/0x235dc.c)`(0xc, 1)` (`"ML close ALSA!!!"`) on close.
- New 2-bit barrier coordinator ([`0x1d388`](functions/0x1d388.c), flag helpers [`0x15284`](functions/0x15284.c)/[`0x1529c`](functions/0x1529c.c)/[`0x152ac`](functions/0x152ac.c) on `DAT_0003bfb5`) waits for both `am_evaluate_vmf_msg_source_service` ([`0x1d3b4`](functions/0x1d3b4.c), bit 1) and `audio_channel_recv_dsp_source_resp` ([`0x1457c`](functions/0x1457c.c), bit 2) before dispatching [`0x1c908`](functions/0x1c908.c)`(0, 8, 0)` on a MirrorLink (`0xc`) → source `8` transition.
