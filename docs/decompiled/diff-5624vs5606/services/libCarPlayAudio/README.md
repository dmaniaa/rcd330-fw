[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/libCarPlayAudio/README.md)

# `opt/lib/libCarPlayAudio.so` — CarPlay ALSA Audio Plugin (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/lib/libCarPlayAudio.so` (`16,976` B vs. `5624` `20,160` B, Δ `-3,184` B, ARMv7-A ELF, SHA-256 `86910a0c1d27d7e52771177233818090ae3f9b233cb3d446846fe3ea1c2359d9`)
- **Decompiled**: `132` written / `189` total / `57` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Older **`CD30_Sept30`** build (`−1,240` B `.text`).
- Lacks `5624`'s built-in VMF client thread (`CarPlayVMFCreate`, `CarPlayVMFStart`, `CarPlayVMFSendMsg`, `CarPlayVMFFinalize`, `ThreadGetMsgFromVmf`) and configurable ALSA default soundcard selection.
