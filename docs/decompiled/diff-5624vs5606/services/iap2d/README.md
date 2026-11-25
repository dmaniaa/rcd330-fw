[← 5606 Diff Index](../../README.md) · [5624 Baseline](../../../5624/services/iap2d/README.md)

# `opt/iap2d` — Apple iAP2 Daemon (`5606_skoda` / `7.1.2.2`)

- **Target binary**: `opt/iap2d` (`158,760` B vs. `5624` `169,152` B, Δ `-10,392` B, ARMv7-A ELF, SHA-256 `fa5fecc463b251f68dfc957c67e843c9d265940b2aa74ecf85cdae0f7853e418`)
- **Decompiled**: `469` written / `546` total / `77` external / `0` failed in [functions/](functions) ([decompile-index.json](decompile-index.json))
- **Full diff**: [diff-5624-vs-5606.md](../../../../diff-5624-vs-5606.md)

## Behavioral Differences vs. `5624_vw`

- Older **`$IAP: 5.2 $` / `CP_IAP2_R3`** (`AUTONAVI_TBOX_R3.0-7-g311d`, `−10,008` B `.text`) vs. `5624`'s `$IAP: 5.3 $` / `CP_IAP2_R4-3`.
- Lacks iAP2 5.3 features added in `5624`: External Accessory Protocol sessions (`IAP2_Start/StopExternalAccessoryProtocolSession`, `IAP2_ExternalAccessoryDataReady`), OOB Bluetooth Pairing (`IAP2_Start/StopOOBBTPairing`, `IAP2_OOBBTPairingLinkKeyInfo`), `IAP2_RequestAccessoryWiFiConfigInfo`, `IAP2_DeviceUUIDNotification`, and custom transport disconnect.
- Retains `IAP2_Authentication{Started,Ended}` and `IAP2_Identification{Started,Ended}` callbacks.
