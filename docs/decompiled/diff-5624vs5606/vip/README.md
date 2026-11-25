[← 5606 Diff Index](../README.md) · [5624 Baseline](../../5624/vip/README.md)

# Decompiled RH850 VIP Firmware (`5606_skoda/vipImage.bin`)

Full Ghidra 12.2 headless decompilation archive of the Renesas RH850/F1x (`R7F7010253`, `V850:LE:32:v850e3v5`) Vehicle Interface Processor firmware from `extracted-fw/5606_skoda/vipImage.bin` (`1,048,588` bytes, SHA-256 `3ae3c736c55c884c06f96d108802bc836dece39f3d8d65e1ae1d3380e2d65e78`, `stdplus.ini` `VIPAP_VER=7.1.1.0`, trailer version bytes `06 05 05 01` @ `0xffffc` vs. `5624` `05 05 06 01`).

- **Exported**: `2,814` functions (`2,814` decompiled, `0` failed) in [functions/](functions), indexed in [functions_index.json](functions_index.json) and [strings.json](strings.json).
- **Reproducible command**:
  ```sh
  ./tools/ghidra/vip_headless.sh vip_5606 -import extracted-fw/5606_skoda/vipImage.bin -overwrite \
    -loader BinaryLoader -loader-baseAddr 0 -processor V850:LE:32:v850e3v5 -scriptPath tools/ghidra \
    -preScript VipSetup.java appMain=0x13de0 rodataStart=0x54b2c rodataEnd=0x61330 \
    -postScript VipExport.java out=docs/decompiled/diff-5624vs5606/vip
  ```
- **Full diff**: [diff-5624-vs-5606.md](../../../diff-5624-vs-5606.md)

## Memory Layout & Behavioral Differences vs. `5624_vw`

- **Stage 0 Boot Manager (`0x00000..0x07FFF`) & Stage 1 Flash Loader (`0x08000..0x0FFFF`)**: **100% byte-identical** to `5624_vw`.
- **Stage 2 Application (`0x10000..`)**: `APP_RESET_handler` @ [`0x45296`](functions/0x45296.c), `APP_main` @ [`0x13de0`](functions/0x13de0.c), `.rodata` @ `0x54b2c`, `.data` ROM `0x5f3c4..0x6123d` → RAM `0xfede8000`, FDL/EEL @ `0x61330` (`DH850T01` @ `0x62890`). `2,595` of `2,814` functions normalize identically to `5624_vw`.
- **Smaller VRM / Part-Number Table ([`0x136ac`](functions/0x136ac.c) / [`0x13b54`](functions/0x13b54.c))**: Supports `13` part numbers (`0..0xc`) and `58` (`0x3a`) model-map entries at `0x54edd`, whereas `5624` expands to `19` part numbers (adding `5G0035280A/B/C`, `6RF035187C/D/E`) and `64` (`0x40`) model-map entries.
- **BAP Date Forwarding Absent ([`0x2dfcc`](functions/0x2dfcc.c) / [`0x2e070`](functions/0x2e070.c))**: `5606` lacks `5624`'s calendar date sync (`year + 2000`, `month`, `day` → `DAT_ram_fee00786..789` → `FUN_ram_00045900(4)`) added to `5624` `0x2e06e` / `0x2e126`.
- **AM/Tuner `0x26b` Retry State Machine Absent ([`0x40d98`](functions/0x40d98.c) / [`0x40e26`](functions/0x40e26.c))**: Lacks `5624`'s special handling for parameter `0x26b` and its 5-attempt retry handler (`5624 0x41160`), as well as `5624`-only helper `0x1808c` and `0x3c29a`.
- **PQ 7-Byte Coding Validator ([`0x1f402`](functions/0x1f402.c))**: Logic is identical to `5624` `0x1f48a`.
- **BAP Stack Banner**: Retains older `20140410_BAP_v1.7.1` string.
