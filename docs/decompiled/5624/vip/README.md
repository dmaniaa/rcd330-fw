[← Decompiled Firmware Index](../README.md)

# Decompiled RH850 VIP Firmware (`5624_vw/vipImage.bin`)

Full Ghidra 12.2 headless decompilation archive of the Renesas RH850/F1x (`R7F7010253`, `V850:LE:32:v850e3v5`) Vehicle Interface Processor firmware from `extracted-fw/5624_vw/vipImage.bin` (`1,048,588` bytes, SHA-256 `16cdd521976589eaaffe8eb86815fa87ac897a41a7f75318dae1a882e5b7980e`).

- **Exported**: `2,824` functions (`2,824` decompiled, `0` failed) in [functions/](functions), indexed in [functions_index.json](functions_index.json) and [strings.json](strings.json).
- **Reproducible scripts**: [tools/ghidra/vip_headless.sh](../../../../tools/ghidra/vip_headless.sh), [tools/ghidra/VipSetup.java](../../../../tools/ghidra/VipSetup.java), [tools/ghidra/VipExport.java](../../../../tools/ghidra/VipExport.java).

---

## 1. Flash & RAM Memory Map

| Range | Region | Notes |
|---|---|---|
| `0x00000..0x07FFF` | Stage 0 Boot Manager (`BM`) | Exception vectors `@0x0`, EIINT table `@0x200`, reset `0x4da0`, `BM_main` `0x3096` |
| `0x08000..0x0FFFF` | Stage 1 Flash Loader (`FL`) | Exception vectors `@0x8000`, EIINT table `@0x8200`, reset `0xca82` |
| `0x10000..0x54E67` | Stage 2 Application code (`APP`) | Exception vectors `@0x10000`, EIINT table `@0x10200` (288 entries), reset `0x455d2`, `APP_main` `0x13de2`, BAP/OPS library `0x45700..0x54e67` |
| `0x54E68..0x5FB1B` | Application `.rodata` | Part-number/VRM tables (`0x55218`, `0x55318`, `0x553ba`), UDS WriteDID table (`0x56cb0`), DTC table (`0x5711e`, 63 entries), EQ profile table (`0x590d0`), CAN ID tables (`0x59376..`) |
| `0x5FB1C..0x619AD` | Initialized `.data` image | Copied to RAM `0xFEDE8000` at reset via copy table `@0x10688`; `.bss` zeroed `0xFEDE9E94..0xFEDEC360` (`@0x10680`) |
| `0x61AA0..0x66FFF` | DataFlash / EEL / FDL driver | Renesas Flash Data Library (`DH850T01xxxxxxV200` / `EH850T01xxxxxxV200`) |
| `0xFEE00000..` | Retained RAM / DataFlash mirror | Adaptation mirror `0xFEE00014..` (block `0xCF`), PQ 7-byte Coding `0xFEE00410..0xFEE00416` (block `0xB7`), DTC state `0xFEE00424..` |
| `0xFF000000..` | Peripheral MMIO | RH850 P-bus / H-bus registers |

Both `gp` and `ep` are `0x0` in all stages (absolute addressing, Green Hills toolchain).

---

## 2. Key Recovered Subsystems & Functions

### UDS / KWP2000 Diagnostics, Coding & Adaptation
- [`0x27cf0`](functions/0x27cf0.c) — Diagnostic service dispatcher (`0x10`, `0x12`, `0x14`, `0x18`, `0x21`, `0x22`, `0x23`, `0x2E`, `0x31`, `0x32`, `0x33`, `0x3D`, `0x82`)
- [`0x1e136`](functions/0x1e136.c) → [`0x1e1b8`](functions/0x1e1b8.c) — UDS `0x2E WriteDataByIdentifier` (table `@0x56cb0`: `0xF187`, `0xF1A3`, `0xF1A0`, `0xF1A1`, `0xF198` fingerprint, `0xF1A4`, `0xF1AC`, `0x0600` coding, `0xF17B`)
- [`0x1f48a`](functions/0x1f48a.c) — **PQ 7-byte Coding (`DID 0x0600`) validator & commit** (bit-for-bit identical in `5606_skoda` at `0x1f402`)
- [`0x1f576`](functions/0x1f576.c) / [`0x1f5b0`](functions/0x1f5b0.c) / [`0x20fdc`](functions/0x20fdc.c) — Persist/read coding DataFlash block `0xB7` & mirror RAM `0xFEE00410..0xFEE00416`
- [`0x21f9a`](functions/0x21f9a.c) — **PQ Adaptation (`Adaptation-10`) handler** for all 13 channels (`2, 3, 5, 10..14, 16, 17, 36, 47, 50`)
- [`0x25e6a`](functions/0x25e6a.c) / [`0x261ba`](functions/0x261ba.c) / [`0x26404`](functions/0x26404.c) / [`0x25a68`](functions/0x25a68.c) — Factory / EOL diagnostic command handler

### Coding Byte Consumers
- **Byte 1 (`0xFEE00411`) — External SoundSystem Amp (`== 0x02`)**: [`0x23388`](functions/0x23388.c) / [`0x412e8`](functions/0x412e8.c) → latched at boot in DSP/amp init [`0x3c362`](functions/0x3c362.c), DSP line-level attenuation [`0x3e942`](functions/0x3e942.c), BAP external amp flag `0x2D` in [`0x2e126`](functions/0x2e126.c), power amp diagnostics [`0x2189c`](functions/0x2189c.c) & [`0x4356a`](functions/0x4356a.c)
- **Byte 2 (`0xFEE00412`) — Speaker Channels (`0x00`/`0x02`/`0x04`)**: [`0x216d0`](functions/0x216d0.c) → forwarded to Linux in [`0x216da`](functions/0x216da.c), speaker channel monitor [`0x4356a`](functions/0x4356a.c), coding plausibility DTC `01044` [`0x2189c`](functions/0x2189c.c), actuator tests [`0x21b84`](functions/0x21b84.c) / [`0x22af8`](functions/0x22af8.c)
- **Byte 3 Bit 1 (`0xFEE00413 & 0x02`) — External Microphone `R38`**: [`0x23398`](functions/0x23398.c) → SAF775x DSP mic input mux [`0x3f6d8`](functions/0x3f6d8.c) / [`0x3f7ba`](functions/0x3f7ba.c), gain init [`0x40e5a`](functions/0x40e5a.c), ADC6−ADC7 current monitor [`0x40e0c`](functions/0x40e0c.c) / [`0x4124e`](functions/0x4124e.c) → DTC `01523` [`0x40da4`](functions/0x40da4.c), MVB [`0x20458`](functions/0x20458.c)
- **Byte 3 Bit 6 (`0xFEE00413 & 0x40`) — Rear View Camera (RVC)**: [`0x20dbc`](functions/0x20dbc.c) → [`0x2e06e`](functions/0x2e06e.c) → [`0x45900`](functions/0x45900.c) → [`0x48fac`](functions/0x48fac.c)
- **Byte 4 (`0xFEE00414`) — Treble Speaker Monitoring (`0..4`)**: [`0x2170a`](functions/0x2170a.c) → channel monitor [`0x435b0`](functions/0x435b0.c), actuator tests [`0x21b84`](functions/0x21b84.c) / [`0x22af8`](functions/0x22af8.c), EQ profile lookup [`0x40560`](functions/0x40560.c)
- **Byte 6 (`0xFEE00416 & 0x3F`) — Vehicle Model (`1..26`)**: [`0x136ac`](functions/0x136ac.c) → [`0x13b56`](functions/0x13b56.c) (table `@0x55218`; CAN fallback `@0x55318`), VRM broadcast [`0x13580`](functions/0x13580.c), EQ profile lookup [`0x40560`](functions/0x40560.c)

### VIP ↔ Linux SPI IPCL Diag Bridge
- [`0x1d604`](functions/0x1d604.c) — TX wrapper for `IPCL(0x07, 0x01)` (`opt/diag`)
- [`0x1d64a`](functions/0x1d64a.c) — RX dispatcher for `IPCL(0x07, 0x00)` from Linux `opt/diag`
- [`0x216da`](functions/0x216da.c) (`actionId=1` speaker count), [`0x23158`](functions/0x23158.c) (`actionId=5` coding), [`0x232b0`](functions/0x232b0.c) (`actionId=6` `"TP:351 DEH:130 SDS:201 KWP:300"`), [`0x232ec`](functions/0x232ec.c) (`actionId=7` Ch 50 EngMode), [`0x23320`](functions/0x23320.c) (`actionId=9` Ch 36 Threshold_High), [`0x23354`](functions/0x23354.c) (`actionId=10` Ch 47 ML_Country_Code), [`0x208cc`](functions/0x208cc.c) (`actionId=0xFE` EOL coding write)
- [`0x13580`](functions/0x13580.c) / [`0x13894`](functions/0x13894.c) — `IPCL(0x0E, 0x02)` VRM platform/brand/vehicle/part-number & DataFlash service
