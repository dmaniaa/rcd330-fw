# VW RCD 330 Headunit Coding & Feature Manager Specification

Technical reference for **VW Long Coding (`DID 0x0600`)**, **Feature Manager (`FEATURE_MANAGER_PSS_BLK.pss`)**, **early-boot INI coding (`/pss1/stdplus.ini`)**, **VRM platform/brand identity**, and **runtime VIP ↔ Linux coding IPC** on the Noname/Visteon RCD 330 (`6RD 035 187 B/C/E/F`, `5624_vw` & `5606_skoda`).

All byte offsets, bitmasks, CRC parameters, validation rules, adaptation channels, and IPC routes below are verified directly against the Ghidra 12.2 decompilation archives in [docs/decompiled/](decompiled/README.md) — covering **both** the **Renesas RH850 VIP firmware** ([docs/decompiled/vip/](decompiled/vip/README.md), `vipImage.bin`, `2,824` functions) **and** Linux userspace (`opt/diag`, `opt/pss`, `opt/pwr_agent`, `opt/vgw`, `opt/hmi`, `opt/bt`, `opt/camera`, `opt/exlap`, `opt/connect_mgr`, `opt/sysupdate`, `opt/audio_proc`).

---

## 1. Persistence Architecture (4 Storage Layers)

Headunit coding and feature enablement span four coordinated storage layers across `/pss1` and `/pss2`:

| Layer | File Path(s) | PSS Block ID / Size | Writers | Readers | Format |
|---|---|---|---|---|---|
| **1. VW Long Coding (`DID 0x0600`)** | `/pss1/DIAG_PSS_CODING.pss` & `/pss2/DIAG_PSS_CODING.pss` | Block `0x50` (80), `data_len=25`, `total_len=33` (`type=2` dual-partition) | `opt/diag` (`Diag_Provide_Coding` [`0x13660`](decompiled/services/diag/functions/0x13660.c)) → `opt/pss` (`VMF 0x8C:0x02`) | `opt/hmi` (`VPSSProxy` [`0x1c5a90`](decompiled/hmi/functions/0x1c5a90.c)), `opt/bt` (`0x14a5c`, offset `0x10` len 1), `opt/exlap` (`0xd1c8`/`0xd4e4`, len `0x19`), `opt/diag` (`0x131b4`) | 8-byte BE PSS header + 25-byte coding payload (PQ uses bytes `0..6`; MQB uses `0..24`) |
| **2. Feature Manager Flags** | `/pss1/FEATURE_MANAGER_PSS_BLK.pss` & `/pss2/FEATURE_MANAGER_PSS_BLK.pss` + `/opt/feature_config.ini` | Block `0x54` (84), `data_len=25`, `total_len=33` (`type=2` dual-partition) | `opt/pss` (`0xdb34`, `0xe0c0`, `0xe44c`) via `opt/pwr_agent` (`VMF 0x8C:0x09`/`0x0A`) and `opt/diag` adaptation (`DIAG_OUT` sub `0x16`) | `opt/pwr_agent` (`0xd134` via `VMF 0x0B:0x06`), `opt/connect_mgr` (`0xc9a0` via `VMF 0x8C:0x08`), `opt/hmi`, `opt/diag_eol` | 8-byte BE PSS header + 25-byte array (11 feature indices `0..10`), overridden by `/opt/feature_config.ini` |
| **3. Early-Boot Fast INI** | `/pss1/stdplus.ini` (seeded on first boot from `/opt/stdplus.ini`, which ships with `BT=0`, `CODINGS=0`) | — | `opt/pwr_agent` (`0xa35c` → `0x1015c` for `[CODING]`; `0xf704`/`0xfc58`/`0xfd30` for `[VIP]`), `opt/sysupdate` (`0x16028`) | `opt/camera` (`0x8adc`), `opt/hmi` `main()` ([`0x10e58`](decompiled/hmi/functions/0x10e58.c)) — read **before** VMF/PSS are running | Standard INI (`[CODING]` & `[VIP]` sections) |
| **4. BT Coding Mirror** | `/pss2/app_data/bt_coding.config` | — | `opt/bt` (`0x87b3c`) | `opt/bt` (`0x879d8`, defaults to `0x2f` if missing) | Single raw byte = MQB Coding Byte 16 (`g_ucBtCoding`) |

### PSS Binary File Format (`opt/pss` `0x9a78`, `0xa848`, `0xb258`)

Every `.pss` file in `/pss1/` and `/pss2/` begins with an **8-byte Big-Endian header** followed by `data_len` payload bytes (`33` bytes total for blocks `0x50` and `0x54`):

| Offset | Size | Endian | Meaning |
|---:|---:|---|---|
| `0x00..0x01` | 2 | BE | **CRC-16/CCITT-FALSE** (`poly = 0x1021`, `init = 0xFFFF`, no reflection, no final XOR; table at `opt/pss` VA `0x10ac0`) computed over `payload[0..data_len-1]` |
| `0x02..0x03` | 2 | BE | `data_len` (`0x0019` = 25 for `DIAG_PSS_CODING.pss` and `FEATURE_MANAGER_PSS_BLK.pss`) |
| `0x04` | 1 | — | Client / version tag (`0x00` from `opt/diag`, `0x01` from Feature Manager) |
| `0x05..0x06` | 2 | BE | Monotonic write counter (incremented on each write) |
| `0x07` | 1 | — | Reserved (`0x00`) |
| `0x08..0x20` | 25 | — | Raw payload bytes (`payload[0..24]`) |

To generate a valid `DIAG_PSS_CODING.pss` offline (e.g. from host tooling):
```python
def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

payload25 = coding_bytes.ljust(25, b"\x00")[:25]
pss_blob = struct.pack(">HHBHB", crc16_ccitt_false(payload25), 25, 0, 1, 0) + payload25
```
Write `pss_blob` identically to both `/pss1/DIAG_PSS_CODING.pss` and `/pss2/DIAG_PSS_CODING.pss`.

---

## 2. `/pss1/stdplus.ini` Coding & VIP Keys

### `[CODING]` Section
Written by `opt/pwr_agent` (`0xa35c` → `0x1015c`) and preserved across firmware updates by `opt/sysupdate` (`0x16028`):

| Key | Value | Meaning |
|---|---|---|
| `CODINGS` | `0..3` (`bt \| (rvc << 1)`) | Bitmask read at early boot by `opt/camera` (`0x8adc`) and `opt/hmi` `main()` (`0x10e58`):<br>• **Bit 0 (`CODINGS & 1`)**: Bluetooth coding (`0` = OFF, `1` = ON)<br>• **Bit 1 (`(CODINGS >> 1) & 1`)**: Rear View Camera (RVC) coding (`0` = OFF, `1` = ON)<br>(`0`=None, `1`=BT only, `2`=RVC only, `3`=BT + RVC) |
| `BT` | `0` or `1` | Mirror of `CODINGS & 1` (written by `opt/sysupdate` `0x16028`; factory `/opt/stdplus.ini` ships `0`) |
| `RVC` | `0` or `1` | Mirror of `(CODINGS >> 1) & 1` (written by `opt/sysupdate` `0x16028`) |

### `[VIP]` Section
Written by `opt/pwr_agent` (`0xf704`, `0xfc58`, `0xfd30`) upon receiving VRM identity (`IPCL 0x0E:0x02`):
- `PLAT`: `PQ` or `MQB`
- `BRAND`: `VW` or `SKODA`
- `CANSPEED`: `HS` (500 kbps) or `LS` (100 kbps)
- `PARTSTR`: 11-character VW part number (e.g. `6RD035187B `)
- `BAP_VER`, `VIPAP_VER`, `VIPBL_VER`: VIP firmware/BAP versions

---

## 3. VW Long Coding (`DID 0x0600` / `DIAG_PSS_CODING.pss`)

### 3.0 Where Coding Actually Lives (VIP + Linux)

When you code the unit with VCDS (`56-Radio` → `Coding-07`) or OBDeleven (`Radio` → `Long Coding`), the tester talks to the **RH850 VIP MCU** (`vipImage.bin`). The VIP owns the KWP2000/UDS stack ([`0x27cf0`](decompiled/vip/functions/0x27cf0.c)), validates writes in [`0x1f48a`](decompiled/vip/functions/0x1f48a.c), persists the authoritative 7-byte PQ coding to DataFlash block `0xB7` (RAM mirror `0xFEE00410..0xFEE00416`), and forwards a copy to Linux `opt/diag` over `IPCL(0x07, 0x01)` (`actionId=0x05`, [`0x23158`](decompiled/vip/functions/0x23158.c), §5.2).

Because both the VIP firmware and Linux userspace are now decompiled, **every single byte and bit below is verified against firmware code** — including several community/VCDS `.lbl` myths inherited from older PQ radios (RCD 310/510/RNS 315) that `vipImage.bin` actually ignores or rejects.

### 3.1 PQ Platform (`platform == 1`, 7 Bytes: `0..6`) — the coding VCDS/OBDeleven shows

#### Exact VIP Write Validation Rules (`vipImage.bin` [`0x1e1b8`](decompiled/vip/functions/0x1e1b8.c) → [`0x1f48a`](decompiled/vip/functions/0x1f48a.c))

The validator below is **bit-for-bit identical in both `5624_vw` (`0x1f48a`) and `5606_skoda` (`0x1f402`)**:
1. **Fingerprint precondition (`0x1e1b8`)**: Tester must first write `DID 0xF198` (6-byte Repair Shop Code / fingerprint, sets `DAT_ram_fede80aa = 1`) before writing `DID 0x0600`, otherwise VIP returns **NRC `0x22`** (*Conditions Not Correct*).
2. **All-zero check**: `sum(byte[0..6]) != 0` (all-zero coding is rejected; an uncoded all-zero block raises VW DTC `01042`).
3. **Byte 0**: `(b0 & 0xE0) == 0` — only bits `4:0` (`0x00..0x1F`) allowed; bits `7:5` rejected.
4. **Byte 1 + Byte 2 + Byte 4 coupling** (all invalid combinations return **NRC `0x31`** *Request Out of Range*):
   - `b1` must be **`0x00`** (internal amp) or **`0x02`** (external digital SoundSystem amp).
   - If `b1 == 0x02`: **`b2` MUST be `0x00`** AND **`b4` MUST be `0x00` or `0x01`** (`b4 < 2`).
   - If `b1 == 0x00`: **`b2` MUST be `0x02` or `0x04`** (`b2 != 0`):
     - `b2 == 0x02` (2 front speakers) → `b4` must be `0x00..0x02`.
     - `b2 == 0x04` (4 speakers) → `b4` must be `0x00..0x04`.
5. **Byte 3**: `(b3 & 0x9D) == 0` in UDS `0x1f48a` (`(b3 & 0xBD) == 0` in EOL `0x25a68`) — **ONLY bits `1` (`0x02`), `5` (`0x20`), and `6` (`0x40`) are permitted!** Bits `0` (`0x01`), `2`, `3`, `4`, and `7` are **rejected with NRC `0x31`**.
6. **Byte 5**: **`b5 == 0x00` is strictly required!** Any non-zero value is **rejected with NRC `0x31`** on both VW `5624` and Skoda `5606`.
7. **Byte 6**: `b6 < 0x40` (`0x00..0x3F`, bits `5:0`).

#### Byte-by-Byte Semantics (VIP + Linux vs. Community Labels)

Verified in `vipImage.bin` (`0xFEE00410..0xFEE00416`) and Linux `opt/diag` ([`0x13660`](decompiled/services/diag/functions/0x13660.c)) / `opt/hmi` ([`0x5ad80`](decompiled/hmi/functions/0x5ad80.c)):

| Byte | Bits / Value | Community / VCDS Label | Verified VIP (`vipImage.bin`) Behavior | Verified Linux (`opt/diag` / `opt/hmi`) Behavior |
|---|---|---|---|---|
| **0** | `bit 0` (`0x01`) | (unused) | Accepted by `0x1f48a` (`& 0xE0 == 0`), **never read** anywhere in VIP | **Not read** |
| **0** | `bit 1` (`0x02`) | (factory `1` in `0x16`) | Accepted, **never read** in VIP | `opt/diag` → Feature `4` `MLINK_ATTR` (overridden `TRUE` on `5624_vw`, §4) |
| **0** | `bit 2` (`0x04`) | (factory `1` in `0x16`) | Accepted, **never read** in VIP | `opt/diag` → Feature `2` `CARPLAY_ATTR` (overridden `TRUE` on `5624_vw`) |
| **0** | `bit 3` (`0x08`) | *"Parking System / OPS"* **(VCDS `.lbl` myth!)** | Accepted, **NEVER read anywhere in VIP!** OPS activates automatically from PDC CAN/BAP frames (`0x497`) regardless of this bit | `opt/diag` → Feature `10` `ANDAUTO_ATTR` (overridden `TRUE` on `5624_vw`, so toggling bit 3 has **zero effect** on either OPS or Android Auto!) |
| **0** | `bit 4` (`0x10`) | (factory `1` in `0x16`) | Accepted, **never read** in VIP | `opt/diag` → Feature `3` `CARLIFE_ATTR` (`INVALID`/forced OFF on `5624_vw`; active on `5606_skoda`) |
| **1** | `0x00` / `0x02` | **Amplifier**: `00` = internal, `02` = external digital amp (Fender / Dynaudio / Skoda Sound System / Beats) | **Verified (`0x23388` / `0x412e8` → `b1 == 2`)**: latched **at boot only** in DSP/amp init [`0x3c362`](decompiled/vip/functions/0x3c362.c) (`DAT_ram_fedeb85f`) — **this is why a restart is required!** Switches power amp to low-level output, disables internal speaker DTCs (`0x2189c`, `0x4356a`), applies DSP attenuation (`0x3e942` → `0x40f9a(2, 599)`), and enables external amp BAP flag `0x2D` (`0x2e126` → `0x458f4`) | Not read directly by Linux |
| **2** | `0x00` / `0x02` / `0x04` | **Speaker channels**: `00` = SoundSystem (req. `B1=02`), `02` = 2 front speakers, `04` = 4 speakers | **Verified (`0x216d0`)**: gates speaker impedance monitoring (`0x4356a`: `02`→front ch `2,3`; `04`→all `0..3`), coding plausibility DTC `01044` (`0x2189c`), actuator tests (`0x21b84`/`0x22af8`), and sends `B2` via `IPCL(0x07, 0x01)` `actionId=1` (`0x216da`) | `opt/diag` `0xe028` → `VDiagProxy+9`: `2` hides HMI Fader controls (§3.4) |
| **3** | `bit 0` (`0x01`) | *"Steering Wheel Buttons"* **(rejected!)** | **REJECTED by `0x1f48a` (`& 0x9D`) & `0x25a68` (`& 0xBD`) with NRC `0x31`!** MFL CAN messages (`0x5C1`) work natively without setting this bit (legacy RCD 310/510 `.lbl` artifact) | Not read |
| **3** | `bit 1` (`0x02`) | *(undocumented in Reddit)* | **Verified (`0x23398`): External Telephone Microphone (`R38`) Enable!** `0` = internal faceplate mic (`SAF775x` `0xF2433D`/`0xF205C8`/`0xF246CE`); `1` = external mic `R38` (`0xF2433E`/`0xF205C9`/`0xF246D3`, [`0x3f6d8`](decompiled/vip/functions/0x3f6d8.c)/[`0x3f7ba`](decompiled/vip/functions/0x3f7ba.c)), loads ext-mic DSP gain (`0x40e5a`), monitors ADC6−ADC7 bias current (`0x40e0c`/`0x4124e`, MVB `0x20458`), and raises VW DTC **`01523`** (`0x40da4`: Open / Short GND / Short B+) | Not read |
| **3** | `bit 5` (`0x20`) | (not in community lists; keep `0`) | Allowed by UDS `0x1f48a`; toggled by EOL `0x02:0x11`/`0x12` (`0x25e6a`); not read by VIP runtime | **Bluetooth DISABLE (INVERTED!)**: `bt_coding = (b3 & 0x20) == 0`. Must stay **`0`** for BT ON (`stdplus.ini` bit 0, `PA_OUT_CODING`) |
| **3** | `bit 6` (`0x40`) | **Rear View Camera (RVC)** | **Verified (`0x20dbc`)**: fed into the VIP ParkPilot/RVC state machine (`0x2e06e` → `0x45900(1, &rvc)` → [`0x48fac`](decompiled/vip/functions/0x48fac.c)) | **Verified (`0x13660`)**: `rvc_coding = (b3 & 0x40) != 0` → `stdplus.ini` bit 1 (`opt/camera` & `opt/hmi`), `PA_OUT_CODING`, Feature `1` |
| **4** | `0x00..0x04` | **Treble speaker monitoring**: `00` not coded, `01` off, `02` front, `03` rear, `04` front + rear | **Verified (`0x2170a`)**: [`0x435b0`](decompiled/vip/functions/0x435b0.c) enables tweeter load checks (`<2` off, `2` front ch `2,3`, `3` rear ch `0,1`, `4` all `0..3`); selects speaker actuator tests (`0x21b84`/`0x22af8`); also selects `b4 == 4` vs `!= 4` row in internal EQ table `0x590d0` (`0x40560`) | Not read |
| **5** | `0x00` only | *"Frequency Response (01..07 Skoda)"* **(rejected!)** | **REJECTED if `!= 0x00` (NRC `0x31`)** on both `5624_vw` (`0x1f48a`) and `5606_skoda` (`0x1f402`), and **never read** in VIP. Internal EQ profiles (`0x40560`, table `0x590d0`) are chosen from **Byte 6** (vehicle) + **Byte 4** (tweeters), not Byte 5 (legacy Swing/Bolero/RCD 310 `.lbl` artifact) | Not read |
| **6** | `bits 5:0` (`0..63`, valid `1..26`) | **Car selection** (OPS / fader car picture) | **Verified (`0x136ac` → `0x13b56`)**: `1..26` selects vehicle model in `0x55218` (sets VRM `vehicle` & `platform`, plus EQ profile in `0x40560`); `0` falls back to CAN vehicle fingerprint table `0x55318` | **Verified (`0x5ad80`)**: `1..26` selects OPS & Fader car silhouette (`0x8de308`, §3.5); `0` falls back to `PA_OUT_NET_SYS_INFO` or default `0x13` (`VW370`) |

#### Decoded Community Strings (all valid under `0x1f48a`)

| Long Coding | B0 | B1 | B2 | B3 | B4 | B5 | B6 | Meaning |
|---|---|---|---|---|---|---|---|---|
| `16020000010005` | `16` (CarLife+CarPlay+MLink) | `02` ext. amp | `00` SoundSystem | `00` BT ON, int. mic, no RVC | `01` treble mon. off | `00` | `05` (`VW411`) | Fender/Dynaudio/Beats, no camera |
| `16020040010005` | `16` | `02` | `00` | `40` **RVC ON** | `01` | `00` | `05` | Fender/Dynaudio/Beats + camera |
| `16000400040005` | `16` | `00` int. amp | `04` 4 speakers | `00` | `04` front+rear tweeters | `00` | `05` | Base 4-speaker audio, no camera |
| `16000440040005` | `16` | `00` | `04` | `40` **RVC ON** | `04` | `00` | `05` | Base 4-speaker audio + camera |

> [!NOTE]
> **Why changing to Fender/Dynaudio requires editing Bytes 1, 2, AND 4 together + restarting**:
> `0x1f48a` rejects `B1=02` unless `B2=00` **and** `B4 <= 01` simultaneously (returning NRC `0x31`). And `0x3c362` only latches `B1 == 2` during VIP DSP/power-amp initialization at boot, so a long-press power restart is mandatory for external amplifier output to take effect.

> [!TIP]
> **External Microphone (`R38`) Retrofit**: To switch from the built-in faceplate microphone to a factory overhead mic (`R38`), set **Byte 3 Bit 1 (`+0x02`)** — e.g. `42` instead of `40` (with RVC) or `02` instead of `00` (without RVC). Note that enabling this bit also activates `R38` impedance monitoring (VW DTC `01523`).

#### Gateway Installation List (prerequisite, not radio coding)
Configure `19-Gateway` → Installation List: select `56-Radio`; deselect `37-Navigation` if replacing an RNS; deselect `77-Telephone` (and physically unplug the factory under-seat Bluetooth module) so it does not clash with the RCD 330's built-in Bluetooth stack.

### 3.2 MQB Platform (`platform == 2`, 25 Bytes: `0..24`)

> [!IMPORTANT]
> The 25-byte layout applies **only** when the VIP reports `platform == 2` (MQB) via VRM (§5.1). A PQ-car install (Golf 6 / Polo 6R / Octavia 2 / Fabia 2 / Yeti, etc.) shows the **7-byte** coding from §3.1 in VCDS/OBDeleven. Do not mix the two tables.

Verified across `opt/diag` (`0x13660`), `opt/hmi` (`VPSSProxy` `0x1c5a90`, `VDiagProxy` `0x1fe10c`, `VBTProxy` `0xc8954`/`0xc89a8`/`0xc89f8`, `VPowerProxy` `0x574cc`/`0x5ad80`, `0x201254`/`0x1dbfa8`), `opt/bt` (`0x14a5c`, `0x879d8..0x87d6c`), and `opt/exlap` (`0xd4e4`):

| Byte | Bits / Mask | Function | Semantics & Consumers |
|---|---|---|---|
| **Byte 0** | `bits 3:0` (`0x0F`) | **Brand Code** | `opt/exlap` `0xd4e4` (`Car_Brand`) |
| **Byte 1** | `bits 3:0` / `bits 7:4` | **Vehicle Class / Generation** | `opt/exlap` `0xd4e4` (`Car_Class`, `Car_Generation`) |
| **Byte 2** | `bits 3:0` / `bits 7:4` | **Body Style (Derivative) / Variant** | `opt/exlap` `0xd4e4` (`Car_Derivative`, `Car_Variant`) |
| **Byte 3..5** | — | Country / Audio / Speaker channels | Handled on VIP MCU; speaker count sent via `actionId=0x01` |
| **Byte 6** | `bits 5:0` (`0x3F`) | **Vehicle Type (PQ fallback)** | Stored by `VPowerProxy` `0x5ad80`; on MQB (`platform==2`), `VPowerProxy::0x5ab70` maps VRM Vehicle (`0x32..0x52`) instead |
| **Byte 7..13** | — | VIP-side only (tuner/FM/antenna/audio) | Not parsed in Linux userspace |
| **Byte 14 (`0x0E`)** | `bit 1` (`0x02`) | **AM Band Disable** | `VPSSProxy` `0x1c5a90` → `VPowerProxy::0x574cc`/`0x574e4`: `1` = disables/skips AM source (`2`) in Radio band switching (`0x4b0c8`, `0x1e82c0`, `0x12226c`, `0x4976c`) |
| **Byte 15 (`0x0F`)** | — | VIP-side only | Not parsed in Linux userspace |
| **Byte 16 (`0x10`)** | **Full Byte** | **Bluetooth Coding (`g_ucBtCoding`, default `0x2F`)** | Read by `opt/diag` `0x13660`, `opt/bt` (`0x14a5c`, `0x87c1c..0x87d6c`), and `opt/hmi` (`VBTProxy` `0xc8954`/`0xc89a8`/`0xc89f8`):<br>• **`bit 0` (`0x01`)**: **BT Enable** (`bt_coding` → `stdplus.ini` bit 0, `PA_OUT_CODING`, Feature ID `0`)<br>• **`bit 1` (`0x02`)**: **BT A2DP Audio** (`(b16 & 0x0B) == 0x0B` required for `BTCODING_UseAudio` & HMI A2DP)<br>• **`bit 2` (`0x04`)**: **BT HFP Phone** (`(b16 & 0x05) == 0x05` required for `BTCODING_UsePhone` & HMI Phone)<br>• **`bit 3` (`0x08`)**: **BT AVRCP** (combined with bits 0 & 1 for `0x0B` Audio)<br>• **`bits 5:4` (`0x30`)**: **BT Visibility Mode** (`0x00`=0 visible/selectable, `0x10`=1, `0x20`=2, `0x30`=3; HMI `VBTProxy+6 = (b16 & 0x30) == 0`) |
| **Byte 17 (`0x11`)** | — | VIP-side only | Not parsed in Linux userspace |
| **Byte 18 (`0x12`)** | **Full Byte (`0x00..0x23`)** | **Startup Boot Screen & Off-Clock Logo ID** | `opt/diag` `0x13660` → `VMF(0xA1, 0x0E, [0xA0, 0x1E, b18])` → `VDiagProxy` `0x1fe10c` → `0x1fb9b8` (flashes `/dev/mtd5` bootlogo); also `VPSSProxy` → `VPowerProxy+0x89` → `VSystemOffClockPage` `0x2e19f0`; also `opt/exlap` `0xd4e4` |
| **Byte 19 (`0x13`)** | `bit 4` (`0x10`) | **Rear View Camera (RVC) Enable** | `opt/diag` `0x13660`: **`rvc_coding = (byte19 & 0x10) != 0`** → updates `stdplus.ini` bit 1, `PA_OUT_CODING` `0x0C:0x0D`, Feature ID `1` |
| **Byte 20..22** | — | VIP-side only | Not parsed in Linux userspace |
| **Byte 23 (`0x17`)** | `bit 0` (`0x01`), `bit 2` (`0x04`) | **Driving Safety / Menu Lockout** | `VPSSProxy` `0x1c5a90` → `0x201254` → `0x1dbfa8`:<br>• **`bit 0` (`0x01`)**: Driving menu lockout enabled (MQB)<br>• **`bit 2` (`0x04`)**: Lock trigger source: `1` = Handbrake/standing (`VBAPProxy::0xb1f14` → popup `0x67` / page `0x96`), `0` = Speed threshold (`VBAPProxy::0xafd64` vs `VDiagProxy::0x1fdcb4` → popup `0x68` / page `0x97`) |
| **Byte 24 (`0x18`)** | — | VIP-side only | Not parsed in Linux userspace |

### 3.3 Startup Screen Logo IDs (MQB Byte 18, `VDiagProxy::0x1fb9b8`)

Three regional banks of 12 logos each (`0x00..0x0B` = ASIA, `0x0C..0x17` = NAR, `0x18..0x23` = EU):

| Offset in Bank | Logo Variant | ASIA | NAR | EU |
|---:|---|---:|---:|---:|
| `+0` | Normal VW | `0x00` | `0x0C` | `0x18` |
| `+1` | Twin Drive | `0x01` | `0x0D` | `0x19` |
| `+2` | GTD | `0x02` | `0x0E` | `0x1A` |
| `+3` | GTI | `0x03` | `0x0F` | `0x1B` |
| `+4` | BlueMotion | `0x04` | `0x10` | `0x1C` |
| `+5` | e-Golf | `0x05` | `0x11` | `0x1D` |
| `+6` | R-Line | `0x06` | `0x12` | `0x1E` |
| `+7` | R | `0x07` | `0x13` | `0x1F` |
| `+8` | Alltrack | `0x08` | `0x14` | `0x20` |
| `+9` | GTE | `0x09` | `0x15` | `0x21` |
| `+10` | Beats Audio | `0x0A` | `0x16` | `0x22` |
| `+11` | Dynaudio | `0x0B` | `0x17` | `0x23` |

### 3.4 Sound / Speaker Configuration (`2` vs `4` Channels)

Neither `opt/hmi` nor `opt/audio_proc` implements a Dynaudio/Fender/Beats EQ/DSP coding byte (external amplifier / speaker impedance switching lives on the VIP RH850 MCU; on Linux, `Beats` and `Dynaudio` only appear as MQB Byte 18 bootlogo variants).

Instead, **speaker count (`2` vs `4`)** is sent by VIP via `IPCL(0x07, 0x01)` (`actionId = 0x01`) → `opt/diag` `0xe028` → `VMF(0x73, 0x13)` → `VDiagProxy + 9` (`FUN_001fc6c8()`):
- **`2` speakers**: Balance-only mode (hides Fader Up/Down controls in `VToneSetupFaderBalance` `0x1c0de4`, changes label in `VSoundSetupMain` `0x1b4ea0`).
- **`4` speakers** (any value `!= 2`): Balance + Fader mode.

### 3.5 Vehicle Model / Silhouette Tables (OPS & Sound Fader Car Image)

**PQ Coding Byte 6 (`1..26`, `opt/hmi` VA `0x8de308`)**:
`1`=VW321, `2`=VW310, `3`=VW351, `4`=VW350, `5`=VW411, `6`=Skoda SK350, `7`=Skoda SK316, `8`=VW351, `9`=VW321, `10`=VW361, `11`=VW463, `12`=VW320, `13`=VW218, `14`=VW210, `15`=VW250, `16`=VW251, `17`=VW361, `18`=VN417, **`19` (`0x13`, default)**=VW370, `20`=Skoda SK351, `21`=Skoda SK260, `22`=Skoda SK251, `23`=VW251, `24`=VW230, `25`=VW231, `26`=VW237.

**MQB VRM Vehicle Byte (`0x32..0x52` → index `1..26` via `VPowerProxy::0x5ab70`, `opt/hmi` VA `0x8de238`)**:
`0x32`→1(VW313), `0x33`→2(VW326), `0x34`→3(VW378), `0x35`→4(SK371), `0x36`→5(SK326), `0x37`→6(SK481), `0x43`→7(SK326), `0x38`→8(VW370), `0x39`→9(VW481), `0x3a`→10(VW370), `0x3e`→11(VW372), `0x44`→12(VW270), `0x45`→13(VW271), `0x46`→14(VW371), `0x47`→15(VW276), `0x48`→16(VW331), default→17(VW370), `0x4b`→19(SK372), `0x4c`→20(VW483), `0x4d`→21(VW421), `0x4e`→22(VW331), `0x4f`→23(VW330), `0x50`→24(VW316), `0x51`→25(VW326), `0x52`→26(VW216).

### 3.6 Adaptation (`Adaptation-10`) & Developer / Engineering Menu

#### PQ Adaptation Channels (`vipImage.bin` [`0x21f9a`](decompiled/vip/functions/0x21f9a.c) — ALL 13 Channels Verified)

On PQ, VCDS `Adaptation-10` uses KWP2000 routines (`0xB8`/`0xB9`/`0xBA`/`0xBB`) handled directly by VIP [`0x21f9a`](decompiled/vip/functions/0x21f9a.c). Every supported channel is listed below:

| VCDS Channel (Dec / Hex) | Range / Default | VIP Storage / Action | Forwarded to Linux (`opt/diag` PQ `cls6` / `cls0`) |
|---:|---|---|---|
| **`2`** (`0x02`) | `0..1` | DataFlash `0xCD` bit 0 (stored/read back only) | — |
| **`3`** (`0x03`) | `1..99` (default `30`) | **Ignition-off auto-shutoff timer (minutes)** (`DAT_ram_fee00133`; `0x15bde` schedules `val * 12000` ticks @ 5 ms) | — |
| **`5`** (`0x05`) | `0..4` | DataFlash `0xCD` bits `3:1` (stored/read back only) | — |
| **`10`** (`0x0A`) | `0..1` | **Production / Test Mode** (`DAT_ram_fedeb199`; must be `1` to unlock channels `11..14, 16, 17`) | `0x230de` → `IPCL(0x07,0x01)` `[5,0,0,0x0A,0,val]` (`opt/diag` PQ `cls0` `0x0a`) |
| **`11`** (`0x0B`) | `1` or `4` *(req. Ch 10=1)* | Test audio source select | `IPCL(0x07,0x01)` `[5,0,0,0x0B,0,val]` |
| **`12`** (`0x0C`) | `1` or `2` *(req. Ch 10=1 & 11=1)* | Tuner test band (FM / AM) via `0x335c0` | — |
| **`13`** (`0x0D`) | `870..1080` *(req. Ch 10=1 & 11=1)* | FM test frequency (`val * 10` kHz = `87.0..108.0 MHz`) | — |
| **`14`** (`0x0E`) | `531..1602` *(req. Ch 10=1 & 11=1)* | AM test frequency (`kHz`) | — |
| **`16`** (`0x10`) | `0..100`% *(req. Ch 10=1)* | Test volume (`round(val * 30 / 100)` → `0..30`) | `IPCL(0x07,0x01)` `[5,0,0,0x10,0,vol]` → `opt/diag` `Diag_Volume_control` (`0xc400`) |
| **`17`** (`0x11`) | `0..4` *(req. Ch 10=1)* | Test mode sub-parameter | `IPCL(0x07,0x01)` `[5,0,0,0x11,0,val]` |
| **`36`** (`0x24`) | `0..255` | `DAT_ram_fee00017` (DataFlash `0xCF`) | `0x23320` (`cls6 actionId=9`) → `opt/diag` `0x100b4` **`Diag_Threshold_High_Notify`** |
| **`47`** (`0x2F`) | `0..15` | `DAT_ram_fee00018` (DataFlash `0xCF`) | `0x23354` (`cls6 actionId=10`) → `opt/diag` `0x101b4` **`Diag_ML_Country_Code_Notify`** (MirrorLink country code) |
| **`50`** (`0x32`) | **`0..1`** | **`DAT_ram_fee00014` (DataFlash `0xCF`) — Developer / Engineering Menu** | **`0x232ec` (`cls6 actionId=7`) → `opt/diag` [`0xb1e8`](decompiled/services/diag/functions/0xb1e8.c) `Diag_Eng_Mode_Notify` → `VMF 0xA1:0x0A` → HMI `VDiagProxy+0xe`** |

**Developer Menu End-to-End Trace (`Channel 50 = 1`)**:
1. VCDS writes **PQ Adaptation Channel 50 (`0x32`) = `1`** → VIP [`0x21f9a`](decompiled/vip/functions/0x21f9a.c) saves `DAT_ram_fee00014 = 1` to DataFlash block `0xCF` and calls [`0x232ec`](decompiled/vip/functions/0x232ec.c).
2. `0x232ec` sends `IPCL(0x07, 0x01)` `[0x04, 0x06, 0x00, 0x07, 0x01]` (`cmd_class=6`, `actionId=7`, also re-sent on boot/sync via `0x1d64a`).
3. Linux `opt/diag` (`Diag_Handle_VIP_Request` `0xabc8`, PQ `cls6` table `@0x14124`) dispatches `actionId=7` to `Diag_Eng_Mode_Notify` ([`0xb1e8`](decompiled/services/diag/functions/0xb1e8.c)) → broadcasts `VMF(0xA1, 0x0A, [1])`.
4. `opt/hmi` `VDiagProxy::RecieveVmfMsg` ([`0x1fe10c`](decompiled/hmi/functions/0x1fe10c.c)) stores `1` at `VDiagProxy+0xe` (getter `0x1fce4c`).
5. `VIPCLNotifyFilter::HandleLPCLHardKeySet` ([`0x42088`](decompiled/hmi/functions/0x42088.c)) opens Engineering Testmode (`page 0x0D`) when **SETUP** reports `IPCL_HARDKEY_BH_ULTRALONG_PRESS` (`para[3] == 9`) **and** `VDiagProxy+0xe == 1`. With Channel 50 = `0`, the ultra-long SETUP press is ignored.

#### MQB Adaptation Notify Table (`opt/diag` `Diag_Adaption_Notify` [`0x13544`](decompiled/services/diag/functions/0x13544.c))

On MQB (`platform == 2`), `opt/diag` receives internal adaptation IDs via table VA `0x14cac` (16-byte rows: `id`, handler, `len`, shadow ptr):

| Internal ID | Len | Handler | Forwarded as |
|---:|---:|---|---|
| `0x16` | 1 | `Diag_Pmode_Notify` (`0xb2e0`) | `VMF 0xA1:0x09` |
| `0x18` | 1 | `Diag_Eng_Mode_Notify` (`0xb1e8`) | `VMF 0xA1:0x0A` → `VDiagProxy+0xe` |
| `0x19` | 1 | `Diag_Summer_Time_Notify` (`0xb6ac`) | — |
| `0x1a` | 60 | `Diag_Car_Func_0B3C_Notify` (`0xbcac`) | — |
| `0x1b` | 2 | `Diag_Sticker_Notify` (`0xb5a0`) | — |
| `0x28` | 1 | `Diag_BT_On_Off_Notify` (`0xc138`) | `VMF 0xA1:0x0B` + PSS `0x51` |
| `0x29` | 1 | `Diag_BT_Sniff_Mode_Notify` (`0xbe70`) | — |
| `0x2a` | 1 | `Diag_BT_Phonebook_Access_Pro_Notify` (`0xbfd4`) | `VMF 0xA1:0x0C` + PSS `0x51` |
| `0x2b` | 8 | `Diag_Car_Func_0B5B_Notify` (`0xbbfc`) | — |
| `0x32` | 30 | `Diag_Vehicle_Cfg_Notify` (`0x102b0`) | — |
| `0x37` | 6 | `Diag_HMI_Shutoff_Threshold_Notify` (`0xbaa0`) | `VMF 0xA1:0x11` (`VDiagProxy+0x80..0x85`) |
| `0x42` / `0x44` / `0x46` | 1 | HMI language / default language / MMI language | — |
| `0x43` | 31 | `Diag_HMI_Version_Notify` (`0xb4a4`) | `VMF 0xA1:0x14` |
| `0x45` | 382 | `Diag_Tel_Audio_Dataset_Write` (`0xbd5c`) | — |

---

## 4. Feature Manager (`FEATURE_MANAGER_PSS_BLK.pss` + `/opt/feature_config.ini`)

11 feature indices (`0..10`), queried by services via `VMF(0x8C, 0x08, [0, reply_grp, 4])` and pushed to `opt/pwr_agent` via `VMF(0x0B, 0x06, 11 bytes)` (`opt/pss` `0xf068`):

| Index | `/opt/feature_config.ini` Key (`[FEATURE_BASIC]`) | `5624_vw` Shipped | `5606_skoda` Shipped | Effect in `opt/pwr_agent` (`0xd134`) / Services |
|---:|---|---|---|---|
| `0` | `BT_ATTR` | `TRUE` | `TRUE` | Broadcast to HMI via `VMF(0x0C, 0x12)` (`VPowerProxy+0x9c`); if `0`, disables `opt/bt` (`grp 6`) & CarPlay (`grp 0xf`) |
| `1` | `RVC_ATTR` | `TRUE` | `TRUE` | RVC feature availability (`pwr_agent+0x6e`) |
| `2` | `CARPLAY_ATTR` | `TRUE` | `TRUE` | If `0`, `pwr_agent` skips `start_carplay.sh` (`grp 0xf`) |
| `3` | `CARLIFE_ATTR` | `INVALID` | `TRUE` | If `0`, `pwr_agent` skips `start_carlife.sh` (`grp 0x11`) |
| `4` | `MLINK_ATTR` | `TRUE` | `TRUE` | MirrorLink availability in `opt/connect_mgr` & `opt/hmi` |
| `5` | `NEWBAP_ATTR` | `TRUE` | `TRUE` | New BAP feature set |
| `6` | `EXLAP_ATTR` | `TRUE` | `TRUE` | If `0`, `pwr_agent` skips `opt/exlap` (`grp 0x12`) |
| `7` | `ETM_ATTR` | `TRUE` | `TRUE` | Engineering Test Mode |
| `8` | `OPS_ATTR` | `TRUE` | `TRUE` | OPS feature flag |
| `9` | `CLIMATE_ATTR` | `TRUE` | `TRUE` | Climate popup feature flag |
| `10` | `ANDAUTO_ATTR` | `TRUE` | `TRUE` | If `0`, `pwr_agent` skips `launch_aa.sh` (`grp 0x13`) |

**Override Rules (`opt/pss` `0xe44c`)**:
- `INVALID`: hard-forced **OFF (`0`)** regardless of `FEATURE_MANAGER_PSS_BLK.pss`.
- `TRUE`: hard-forced **ON (`1`)** (RAM shadow forced `0xFF`). Because `5624_vw` ships with all keys `TRUE` except `CARLIFE_ATTR=INVALID`, dynamic coding toggles in block `0x54` only gate App-Connect features when the corresponding key in `/opt/feature_config.ini` is `FALSE`.
- `FALSE`: controlled dynamically by PSS block `0x54` (`!= 0`; for BT `& 1`, for RVC `& 0x11`).
- On MQB, UDS Adaptation `DIAG_OUT` sub `0x16` (`opt/pss` `0x9e18`) also updates Feature Manager (`data[7]>>6&1`=CarLife, `data[7]>>7`=AndroidAuto, `data[8]&1`=CarPlay, `data[8]>>5&1`=MirrorLink).

---

## 5. Runtime VIP ↔ Linux Wire Protocols

### 5.1 VRM Platform / Brand Identity — **Prerequisite for Coding, OPS & PQ Temp**

- **Route**: `IPCL(0x0E, 0x02)` → `opt/vgw` BIF2 → `VMF(0x10, 0x05)` → `opt/pwr_agent` (`0xafe4`) → broadcasts `PA_OUT_VRM_INTO` `VMF(0x0C, 0x06, 15 B)` to `opt/hmi`, `opt/diag`, etc., and persists `[VIP]` in `/pss1/stdplus.ini`.
- **Payload (17 bytes)**:
  ```text
  [0]      = 0x02                 (sub-command: VRM info)
  [1]      = platform             (1 = PQ, 2 = MQB)
  [2]      = brand                (1 = SKODA, 2 = VW)
  [3]      = vehicle              (PQ: 0x00..; MQB: 0x32..0x52, e.g. 0x38 = VW370)
  [4]      = part_num_index       (0x00)
  [5..15]  = part_num_str[11]     (ASCII, e.g. "6RD035187B ")
  [16]     = can_speed            (1 = HS 500 kbps, 2 = LS 100 kbps)
  ```
- Without VRM (`platform == 0`), `opt/diag` rejects VIP coding writes (`DAT_00021d55 == 0`), `opt/hmi` fails the PQ OPS gate (`VBAPProxy::FUN_000afbe4`), and PQ exterior temp `0 → 0xFF` invalidation is bypassed.

### 5.2 Live Long Coding Write (VIP → `opt/diag` → `opt/pss` + `opt/pwr_agent` + `opt/hmi`)

- **Route**: `IPCL(0x07, 0x01)` → `opt/vgw` BIF2 → `VMF(0xA5, 0x02)` → `opt/diag` (`Diag_Handle_VIP_Request` `0xabc8` → `Diag_Provide_Coding` `0x13660`).
- **Payload (`4 + N` bytes)**:
  ```text
  [0]        = 3 + N              (0x0A for PQ 7 B; 0x1C for MQB 25 B)
  [1]        = 0x06               (cmd_class = Provide)
  [2]        = 0x00               (reserved)
  [3]        = 0x05               (actionId = Coding)
  [4..4+N-1] = coding[0..N-1]     (7 bytes PQ or 25 bytes MQB)
  ```
- **Side Effects Executed by `opt/diag`**:
  1. Sends `VMF(0x0B, 0x04, 16 B, [0x05, bt, rvc, carplay, carlife, mlink, 0,0,0,0,0, andauto, 0,0,0,0])` to `opt/pwr_agent`.
  2. `opt/pwr_agent` writes `/pss1/stdplus.ini` `[CODING] CODINGS = bt | (rvc << 1)` (`fsync`), updates `FEATURE_MANAGER_PSS_BLK.pss`, and broadcasts `PA_OUT_CODING` `VMF(0x0C, 0x0D, [bt, rvc])` to `opt/hmi` (updating `VPowerProxy::m_rvc_coding_flag` live).
  3. On MQB, sends `VMF(0xA1, 0x0E, [0xA0, 0x1E, coding[18]])` to `VDiagProxy` (bootlogo).
  4. Writes `coding[0..N-1]` to PSS Block `0x50` (`/pss1/DIAG_PSS_CODING.pss` & `/pss2/DIAG_PSS_CODING.pss`).
  5. Replies to VIP on `VMF(0x8F, 0x21)` → `IPCL(0x07, 0x00)` `[0x04, 0x06, 0x00, 0x05, 0x01]`.

### 5.3 Live Speaker Count & MQB Car Menu Adaptation

- **Speaker Count (`2` or `4`)**: `IPCL(0x07, 0x01)` `[0x04, 0x06, 0x00, 0x01, speaker_count]` → `opt/diag` `0xe028` → `VMF(0x73, 0x13, [speaker_count])` → `VDiagProxy + 9`.
- **MQB BAP Car Menu Adaptation** (`Car_Function_List_BAP_Gen2`): `IPCL(0x07, 0x01)` `[len, 0x06, 0x00, 0x04, data[48]]` → `opt/diag` `0x13358` → `VMF(0xA1, 0x0F)` (or directly via `IPCL(0x0C, 0x02)` `[0x70, data[48]]` → `VBAPProxy` `0xb9970`) → `VDiagProxy::0x1fd628`. `data[2] & 1` = OPS BAP enabled (`+0x14`), `(data[2] >> 1) & 1` = OPS BAP operable (`+0x2e`).

### 5.4 RVC & OPS Screen Activation (`IPCL 0x0C:0x02` → `VMF 0x70:0x01`)

- **OPS Sensor Count (`0x0C`)**: `[0x0C, n_front, n_rear, n_right, n_left]` (e.g. `[0x0C, 4, 4, 0, 0]` for 8K front+rear, `[0x0C, 4, 4, 4, 4]` for 360°; `0, 0, 0, 0` falls back to 4K rear-only).
- **OPS Distances (`0x01`..`0x04`)**: `[0x01, vla, vlm, vrm, vra]` (front), `[0x03, hla, hlm, hrm, hra]` (rear), `[0x02, ...]` (right), `[0x04, ...]` (left). Stepped every 15 units (`0..15` = closest bar 0; corners draw `0..60`, front middle `0..120`, rear middle `0..165`; `0xFF` = clear).
- **Popup Activation (`CarIfPopupCmd_Acknowledge`, `0x4B`)**: `[0x4B, 0x02, mode, rvc_flag]`:
  - `mode = 0`: Close OPS (`0x0b`) and RVC (`0x0e`).
  - `mode = 1` / `2`: Show OPS (`0x0b` / page `0x1a` `vcar_ops`). Requires `platform == 1` (PQ) or MQB OPS BAP adaptation + FSG availability, and `KL_15 == 1`.
  - `mode = 3`, `rvc_flag = 1`: Show RVC (`0x0e` / page `0xdf`) via `VCameraTask` video-lock check.
  - `mode = 4`: Show RVC (`0x0e` / page `0xdf`) immediately without V4L2 lock delay.
  - `mode = 5`: Toggle between OPS (`0x0b`) and RVC (`0x0e`).
- **HMI Popup Control Requests (`IPCL 0x0C:0x01` ← `VMF 0x8F:0x24`)**: When the user taps the OPS/RVC switch button or cancel `X` on screen, `opt/hmi` sends `[0x49, cmd, mode]` to VIP, expecting a matching `[0x4B, 0x02, new_mode, rvc_flag]` acknowledge back on `IPCL(0x0C, 0x02)`.
