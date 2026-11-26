# Emulator Plan 1: VIP PSS Warm-RAM Backup, Deterministic Language & Protocol/Shim Bugfixes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the `opt/pss` boot-time stall and non-deterministic language race in QEMU, fix verified protocol/truncation/logging bugs in `rcd330_vip.c` and `hal_gpio_shim.c`, and sync headless VIP coding defaults with `--coding-file` / `--fw`.

**Architecture:** Decompiling `opt/pss` (`0x8e90`, `0x9a78`, `0x9c30`, `0xb438`, `0xb54c`), `opt/vgw` (`0x3221..0x33bb`), and `vipImage.bin` (`0x2d6e4`) showed why `opt/pss` stalls at boot and answers early reads with status 10 ("not ready"): `rcd330-vip` never answers `IPCL(0x0A, 0x04)` (`request_vip_warm_ram_data`), forcing 4 sequential timeouts. We add a 326-byte warm-RAM buffer to `rcd330-vip` persisted across SoC resets, add optional `HMI_SYSTEM_SETUP_PSS_NEWBLK.pss` (block `0x4b`) seeding and `LANG` CAN control for deterministic language testing, fix the mislabeled `LIGHTS`→`HANDBRAKE` (`IPCL 0x06:0x03`), expand `RAW` to arbitrary length, add qdev startup properties for headless runs, and fix `hal_gpio_shim.c` (`g_rx_stash` 1024 B, 10-arg `log_simple` with `QEMU_LOG_TAGS`, immediate `SIGIO` on TX).

**Tech Stack:** C (QEMU `qemu/hw/misc/rcd330_vip.c`, guest shim `tools/arm_hal_shim/hal_gpio_shim.c`), C++20/Qt6 (`tools/rcd330_gui/`), Python 3 (`tools/rcd330_coding.py`, `tools/hmi_qemu.py`, `tools/tests/`), Bash (`tools/build_rootfs.sh`, `tools/run_rcd330.sh`).

**Spec:** `docs/ipc-and-vgw.md`, `docs/coding.md`, `docs/hmi-internals.md` (§7), `docs/decompiled/5624/services/pss/functions/0xb438.c`, `docs/decompiled/5624/services/pss/functions/0xb54c.c`, `docs/decompiled/5624/services/pss/functions/0x9c30.c`, `docs/decompiled/5624/vip/functions/0x2d6e4.c`.

## Global Constraints

- Never commit `original-fw/`, `extracted-fw/`, or `build/`.
- All QEMU device edits live in the `qemu/` submodule (`rcd330` branch) and are committed there separately before updating the submodule pointer in the superproject.
- Keep `tools/prebuilt/` (`libhal_gpio_shim.so`) updated via `./tools/build_shims.sh --force --update-prebuilt` whenever `tools/arm_hal_shim/hal_gpio_shim.c` changes, and verify glibc 2.13 compatibility.
- Maintain backwards compatibility with existing VIP socket commands (`LIGHTS` continues to be accepted alongside `HANDBRAKE`; `RAW` keeps working with 2-byte invocations as well as longer hex payloads).
- Existing tests (`python3 -m unittest discover -s tools/tests -p "test_*.py" -v` and `./tools/build_gui.sh --test`) must continue to pass.

## Review Focus

- **Warm-RAM range/offset bounds (`IPCL 0x0A:0x04` / `0x0A:0x05`)**: If `opt/pss` requests or writes an offset/length exceeding the 512 B VIP warm-RAM buffer (or `plen < 4`), `rcd330_vip.c` must clamp/reject cleanly without out-of-bounds reads/writes; pinned in Task 1's test.
- **Warm-RAM persistence across SoC reset (`POWER 10s` hold)**: `vip_reset_hold` must NOT zero `warm_ram[]`, matching how EEPROM (`platform`, `coding`) survives SoC reset; pinned in Task 1's test.
- **PSS block `0x4b` CRC & unseeded cold-boot fallback**: Seeding `HMI_SYSTEM_SETUP_PSS_NEWBLK.pss` must remain opt-in (or configurable to `"none"`) so cold-boot fallback behavior can still be tested, and when seeded must carry a valid CRC-16/CCITT-FALSE header matching `opt/pss` `0xa848`/`0xb258` (`data_len = 20`, total `28` bytes); pinned in Task 2's test.
- **`RAW` odd-length / malformed hex or oversized frame**: `RAW` with invalid hex characters, odd nibble count, or payload exceeding `IPCL_MAX_FRAME - 8` must return `ERR RAW\n` without enqueuing a corrupted frame; pinned in Task 3's test.
- **`hal_gpio_shim.c` `log_simple` format with >4 args and `QEMU_LOG_TAGS`**: Forwarding 10 `unsigned long` args must never crash when fewer args are passed (ARM AAPCS passes extras in registers/stack safely for printf-style forwarders) and when `QEMU_LOG_TAGS` is unset must preserve the exact default `"==== HMI ===="` behavior expected by `tools/hmi_qemu.py`; pinned in Task 4's test.

---

### Task 1: Implement VIP PSS Warm-RAM Backup Protocol (`IPCL 0x0A:0x04` / `0x0A:0x05` / `0x0A:0x06` → `0x0A:0x02`)

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c:112-135,618-843,1514-1595`
- Create: `tools/tests/test_emu_plan1_vip_pss_and_shims.py`

**Interfaces:**
- Consumes: `vip_enqueue_ipcl(Rcd330VipState *s, uint8_t grp, uint8_t sub, const uint8_t *payload, uint16_t plen)` in `qemu/hw/misc/rcd330_vip.c`.
- Produces:
  - `uint8_t pss_warm_ram[512]` (non-volatile across `vip_reset_hold`, zero-initialized in `vip_realize`) in `Rcd330VipState`.
  - Handler for `IPCL(0x0A, 0x04)` (`VMF 0x8D:0x01`, `request_vip_warm_ram_data`): replies immediately with `IPCL(0x0A, 0x02)` (`VMF 0x97:0x02`, `PWR_IPCL_MSG_BACKUP_DATA`, payload `[addr_hi, addr_lo, len_hi, len_lo, 0x00, pss_warm_ram[addr..addr+len-1]]`).
  - Handler for `IPCL(0x0A, 0x05)` (`VMF 0x8D:0x02`, warm-RAM write): copies `pl[5..5+len-1]` into `pss_warm_ram[addr..addr+len-1]` when `addr + len <= sizeof(s->pss_warm_ram)`.
  - Handler for `IPCL(0x0A, 0x06)` (`VMF 0x8D:0x03`, `update_vip_block_info`): consumes silently without error.

- [ ] **Step 1: Write the failing test in `tools/tests/test_emu_plan1_vip_pss_and_shims.py`**

```python
#!/usr/bin/env python3
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
VIP_C = ROOT / "qemu/hw/misc/rcd330_vip.c"


class TestEmuPlan1VipPssAndShims(unittest.TestCase):
    def test_vip_handles_pss_warm_ram_backup_and_persists_across_reset(self):
        src = VIP_C.read_text()
        self.assertIn("pss_warm_ram", src)
        self.assertIn("grp == 0x0a && sub == 0x04", src)
        self.assertIn("grp == 0x0a && sub == 0x05", src)
        self.assertIn("vip_enqueue_ipcl(s, 0x0a, 0x02", src)
        # Bounds check against sizeof(s->pss_warm_ram)
        self.assertIn("sizeof(s->pss_warm_ram)", src)
        # Non-volatile: initialized in vip_realize, NOT cleared in vip_reset_hold
        reset_body = src.split("vip_reset_hold(")[1].split("vip_reset_exit(")[0]
        self.assertNotIn("pss_warm_ram", reset_body)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: FAIL (`pss_warm_ram` not found in `rcd330_vip.c`).

- [ ] **Step 3: Implement VIP PSS warm-RAM backup in `qemu/hw/misc/rcd330_vip.c`**

1. In `struct Rcd330VipState` (around line 125, alongside non-volatile EEPROM state), add:
```c
    /* Non-volatile VIP warm RAM for opt/pss type-3 blocks 0..8 (326 bytes used) */
    uint8_t pss_warm_ram[512];
```
2. In `vip_handle_host_frame` (before the `HOST_TX` notification around line 838), add:
```c
        /* opt/pss VIP warm-RAM backup (VMF 0x8D:0x01..0x03 -> IPCL 0x0A:0x04..0x06) */
        if (grp == 0x0a && sub == 0x04 && plen >= 4) {
            /* request_vip_warm_ram_data (opt/pss 0xb438): [addr_hi, addr_lo, len_hi, len_lo, ...] */
            uint16_t addr = ((uint16_t)pl[0] << 8) | pl[1];
            uint16_t rlen = ((uint16_t)pl[2] << 8) | pl[3];
            if ((uint32_t)addr < sizeof(s->pss_warm_ram)) {
                if ((uint32_t)addr + rlen > sizeof(s->pss_warm_ram)) {
                    rlen = (uint16_t)(sizeof(s->pss_warm_ram) - addr);
                }
                if (rlen > IPCL_MAX_FRAME - 16) {
                    rlen = IPCL_MAX_FRAME - 16;
                }
                uint8_t resp[5 + sizeof(s->pss_warm_ram)];
                resp[0] = (uint8_t)((addr >> 8) & 0xff);
                resp[1] = (uint8_t)(addr & 0xff);
                resp[2] = (uint8_t)((rlen >> 8) & 0xff);
                resp[3] = (uint8_t)(rlen & 0xff);
                resp[4] = 0x00;
                memcpy(&resp[5], &s->pss_warm_ram[addr], rlen);
                /* Replies on IPCL(0x0A, 0x02) -> VMF(0x97, 0x02) PWR_IPCL_MSG_BACKUP_DATA */
                vip_enqueue_ipcl(s, 0x0a, 0x02, resp, (uint16_t)(5 + rlen));
            }
        } else if (grp == 0x0a && sub == 0x05 && plen >= 5) {
            /* opt/pss warm-RAM write: [addr_hi, addr_lo, len_hi, len_lo, blk_id, data...] */
            uint16_t addr = ((uint16_t)pl[0] << 8) | pl[1];
            uint16_t wlen = ((uint16_t)pl[2] << 8) | pl[3];
            if (wlen <= plen - 5 &&
                (uint32_t)addr + wlen <= sizeof(s->pss_warm_ram)) {
                memcpy(&s->pss_warm_ram[addr], &pl[5], wlen);
            }
        } else if (grp == 0x0a && sub == 0x06) {
            /* update_vip_block_info (opt/pss 0xb80c) — accepted silently */
        }
```
3. In `vip_realize` (around line 1531), initialize once:
```c
    memset(s->pss_warm_ram, 0, sizeof(s->pss_warm_ram));
```
Do **not** clear `s->pss_warm_ram` in `vip_reset_hold` so warm RAM persists across `POWER 10s` SoC resets.

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: emulate opt/pss warm-RAM backup (IPCL 0x0A:0x04..0x06 -> 0x0A:0x02)"
git add tools/tests/test_emu_plan1_vip_pss_and_shims.py
git commit -m "tests: add Plan 1 VIP PSS warm-RAM backup test"
```

---

### Task 2: Deterministic Boot Language via Optional Block `0x4b` Seeding & VIP `LANG` Control

**Files:**
- Modify: `tools/rcd330_coding.py:180-275,513-535`
- Modify: `qemu/hw/misc/rcd330_vip.c:95-105,307-335,1218-1230,1565-1575`
- Modify: `tools/tests/test_emu_plan1_vip_pss_and_shims.py`

**Interfaces:**
- Consumes: `build_pss_block(payload, data_len, client_tag, counter)` in `tools/rcd330_coding.py`; `vip_send_bem_car_info(Rcd330VipState *s)` in `qemu/hw/misc/rcd330_vip.c`.
- Produces:
  - Optional `"language"` field in `rcd330_coding.py` config (`"auto"` [default: unseeded, preserves cold-boot race testing], `"en_US"` → `0`, `"zh_CN"` → `1`, `"pt_BR"`/`"de_DE"` → `2`, `"ru_RU"` → `3`, `"es_MX"` → `4`). When not `"auto"`, `seed_rootfs_coding` writes valid 28-byte `HMI_SYSTEM_SETUP_PSS_NEWBLK.pss` (`data_len=20`, `payload[0x0b] = lang_idx`, CRC-16/CCITT-FALSE) to `/pss1` and `/pss2`.
  - `LANG <valid> <code>` VIP command in `rcd330_vip.c` controlling `s->can_lang_valid` (`pl[0]`) and `s->can_lang_code` (`pl[1]`) in `vip_send_bem_car_info` (`IPCL 0x06:0x08`).

- [ ] **Step 1: Add failing tests for block `0x4b` seeding and VIP `LANG` command**

Append to `TestEmuPlan1VipPssAndShims` in `tools/tests/test_emu_plan1_vip_pss_and_shims.py`:

```python
    def test_coding_seeds_optional_system_setup_block_0x4b(self):
        import struct
        import sys
        import tempfile
        sys.path.insert(0, str(ROOT / "tools"))
        import rcd330_coding

        with tempfile.TemporaryDirectory() as td:
            # Default "auto" does not write block 0x4b (preserves unseeded cold-boot path)
            cfg_auto = rcd330_coding.default_coding_config("5624_vw")
            self.assertEqual(cfg_auto["language"], "auto")
            rcd330_coding.seed_rootfs_coding(td, cfg_auto)
            self.assertFalse((pathlib.Path(td) / "pss1/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").exists())

            # Explicit language writes valid 28-byte CRC-checked PSS block to pss1 & pss2
            cfg_en = dict(cfg_auto, language="en_US")
            rcd330_coding.seed_rootfs_coding(td, cfg_en)
            p1 = (pathlib.Path(td) / "pss1/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").read_bytes()
            p2 = (pathlib.Path(td) / "pss2/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").read_bytes()
            self.assertEqual(p1, p2)
            self.assertEqual(len(p1), 28)
            crc, dlen, tag, cnt, rsv = struct.unpack(">HHBHB", p1[:8])
            payload = p1[8:]
            self.assertEqual(dlen, 20)
            self.assertEqual(crc, rcd330_coding.crc16_ccitt_false(payload))
            self.assertEqual(payload[0x0B], 0)  # en_US = 0

    def test_vip_supports_can_lang_command(self):
        src = VIP_C.read_text()
        self.assertIn("can_lang_valid", src)
        self.assertIn("can_lang_code", src)
        self.assertIn('sscanf(line, "LANG %d %d"', src)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: FAIL (`KeyError: 'language'` and `can_lang_valid` not in `rcd330_vip.c`).

- [ ] **Step 3: Implement block `0x4b` seeding in `tools/rcd330_coding.py` and `LANG` in `qemu/hw/misc/rcd330_vip.c`**

1. In `tools/rcd330_coding.py`:
   - Add language map near top:
     ```python
     SYSTEM_LANGUAGES: dict[str, int] = {
         "en_US": 0,
         "zh_CN": 1,
         "pt_BR": 2,
         "de_DE": 2,
         "ru_RU": 3,
         "es_MX": 4,
     }
     ```
   - Add `"language": "auto"` to `default_coding_config()`, normalize it in `normalize_coding_config()` (accepting `"auto"` or keys in `SYSTEM_LANGUAGES`, fallback `"auto"`), and include `"language"` in `apply_coding_update()` merge keys.
   - In `seed_rootfs_coding()`, when `norm["language"] in SYSTEM_LANGUAGES`:
     ```python
     lang_name = norm.get("language", "auto")
     if lang_name in SYSTEM_LANGUAGES:
         setup_payload = bytearray(20)
         setup_payload[0x03] = 0x01  # valid init marker matching real 5814 dump
         setup_payload[0x05] = 0x01
         setup_payload[0x06] = 0x01
         setup_payload[0x0B] = SYSTEM_LANGUAGES[lang_name]
         setup_blob = build_pss_block(bytes(setup_payload), data_len=20, client_tag=0, counter=1)
         (pss1 / "HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").write_bytes(setup_blob)
         (pss2 / "HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").write_bytes(setup_blob)
     ```
2. In `qemu/hw/misc/rcd330_vip.c`:
   - Add `uint8_t can_lang_valid;` and `uint8_t can_lang_code;` to `Rcd330VipState`, zeroed in `vip_reset_hold`.
   - In `vip_send_bem_car_info` (line 321), set:
     ```c
     pl[0] = s->can_lang_valid ? 1 : 0;
     pl[1] = s->can_lang_code;
     ```
   - In `vip_process_cmd`, add:
     ```c
     } else if (sscanf(line, "LANG %d %d", &a, &b) == 2) {
         s->can_lang_valid = a ? 1 : 0;
         s->can_lang_code = (uint8_t)(b & 0xff);
         vip_send_bem_car_info(s);
         vip_notify_host(s, "OK LANG\n");
     ```

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: add LANG command for CAN language bytes in IPCL 0x06:0x08"
git add tools/rcd330_coding.py tools/tests/test_emu_plan1_vip_pss_and_shims.py
git commit -m "tools/rcd330_coding: support optional block 0x4b language pre-seeding"
```

---

### Task 3: Fix `HANDBRAKE` (`0x06:0x03`), Arbitrary-Length `RAW`, and Headless VIP qdev Defaults

**Files:**
- Modify: `qemu/hw/misc/rcd330_vip.c:95-125,1239-1246,1372-1376,1514-1536,1605-1610`
- Modify: `tools/run_rcd330.sh:170-265`
- Modify: `tools/tests/test_emu_plan1_vip_pss_and_shims.py`

**Interfaces:**
- Consumes: `vip_parse_hex_bytes(const char *hex, uint8_t *out, int max_len)` in `qemu/hw/misc/rcd330_vip.c`; `tools/rcd330_coding.py load` JSON output in `tools/run_rcd330.sh`.
- Produces:
  - `HANDBRAKE <0|1>` command sending `IPCL(0x06, 0x03)` `[handbrake, 0]` (`LIGHTS` kept as alias updating the same byte for backwards compatibility).
  - `RAW <grp_hex> <sub_hex> [hex_bytes...]` supporting `0..(IPCL_MAX_FRAME - 8)` payload bytes and returning `ERR RAW\n` on invalid hex or overflow.
  - Qdev properties on `rcd330-vip`: `platform` (`uint8`), `brand` (`uint8`), `vehicle` (`uint8`), `can-speed` (`uint8`), `speakers` (`uint8`), `eng-mode` (`uint8`), `part-num` (`string`), `coding-hex` (`string`), applied in `vip_realize` so `--headless` launches match `--coding-file` / `--fw`.

- [ ] **Step 1: Add failing tests for `HANDBRAKE`, arbitrary-length `RAW`, and headless VIP properties**

Append to `TestEmuPlan1VipPssAndShims` in `tools/tests/test_emu_plan1_vip_pss_and_shims.py`:

```python
    def test_vip_handbrake_arbitrary_raw_and_qdev_properties(self):
        src = VIP_C.read_text()
        self.assertIn('sscanf(line, "HANDBRAKE %d"', src)
        self.assertIn("handbrake", src)
        # RAW uses vip_parse_hex_bytes instead of 4-int sscanf
        self.assertNotIn('sscanf(line, "RAW %x %x %x %x"', src)
        self.assertIn("ERR RAW\\n", src)
        for prop in ('"platform"', '"brand"', '"vehicle"', '"coding-hex"', '"eng-mode"'):
            self.assertIn(prop, src)

    def test_run_rcd330_passes_vip_coding_globals_in_dry_run(self):
        import subprocess
        out = subprocess.check_output(
            [str(ROOT / "tools/run_rcd330.sh"), "--headless", "--dry-run"],
            text=True,
        )
        self.assertIn("rcd330-vip.platform=", out)
        self.assertIn("rcd330-vip.coding-hex=", out)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: FAIL (`HANDBRAKE`, `ERR RAW`, and `rcd330-vip.platform=` missing).

- [ ] **Step 3: Implement `HANDBRAKE`, variable-length `RAW`, and qdev properties in `rcd330_vip.c` + `run_rcd330.sh`**

1. In `qemu/hw/misc/rcd330_vip.c`:
   - Add `uint8_t handbrake;` and qdev property backing fields (`uint8_t prop_platform, prop_brand, prop_vehicle, prop_can_speed, prop_speakers, prop_eng_mode; char *prop_part_num; char *prop_coding_hex;`) to `Rcd330VipState`.
   - In `vip_process_cmd`:
     ```c
     } else if (sscanf(line, "HANDBRAKE %d", &a) == 1) {
         s->handbrake = (uint8_t)(a ? 1 : 0);
         /* IPCL(0x06, 0x03) -> VMF(0xA2, 0x02): park-brake / handbrake flag (vipImage.bin 0x2d6e4) */
         uint8_t pl[2] = { s->handbrake, 0 };
         vip_enqueue_ipcl(s, 0x06, 0x03, pl, sizeof(pl));
         vip_notify_host(s, "OK HANDBRAKE\n");
     ```
     Keep `LIGHTS %d` setting `s->lights_mask` and sending `OK LIGHTS\n` (still sending `IPCL 0x06:0x03` only if needed for backwards compatibility or noting deprecation).
   - Replace `RAW` handler with variable-length hex parser:
     ```c
     } else if (!strncmp(line, "RAW ", 4)) {
         unsigned int grp = 0, sub = 0;
         int consumed = 0;
         if (sscanf(line + 4, "%x %x%n", &grp, &sub, &consumed) == 2 &&
             grp <= 0xff && sub <= 0xff) {
             const char *rest = line + 4 + consumed;
             while (*rest == ' ' || *rest == '\t') {
                 rest++;
             }
             uint8_t pl[IPCL_MAX_FRAME - 8];
             int plen = 0;
             bool ok = true;
             if (*rest != '\0') {
                 /* Check for odd hex digit count or invalid chars via strict parse */
                 const char *p = rest;
                 int nibs = 0;
                 while (*p) {
                     if (*p == ' ' || *p == '\t' || *p == ':') { p++; continue; }
                     if (g_ascii_xdigit_value(*p) < 0) { ok = false; break; }
                     nibs++;
                     p++;
                 }
                 if (!ok || (nibs & 1) != 0 || (nibs / 2) > (int)sizeof(pl)) {
                     ok = false;
                 } else {
                     plen = vip_parse_hex_bytes(rest, pl, sizeof(pl));
                     if (plen < 0) ok = false;
                 }
             }
             if (ok) {
                 vip_enqueue_ipcl(s, (uint8_t)grp, (uint8_t)sub, pl, (uint16_t)plen);
                 vip_notify_host(s, "OK RAW\n");
             } else {
                 vip_notify_host(s, "ERR RAW\n");
             }
         } else {
             vip_notify_host(s, "ERR RAW\n");
         }
     ```
   - Register `DEFINE_PROP_UINT8` / `DEFINE_PROP_STRING` in `vip_properties[]` (defaults: `platform=1, brand=2, vehicle=0x13, can-speed=1, speakers=4, eng-mode=0, part-num=NULL, coding-hex=NULL`) and apply them in `vip_realize` (falling back to VW PQ defaults when unset).
2. In `tools/run_rcd330.sh`:
   - Query `python3 "$ROOT_DIR/tools/rcd330_coding.py" load --coding-file "$CODING_FILE" --fw-id "$FW_ID"` once and append `-global rcd330-vip.platform=...`, `brand=...`, `vehicle=...`, `can-speed=...`, `speakers=...`, `eng-mode=...`, `part-num=...`, `coding-hex=...` to `QEMU_CMD`.

- [ ] **Step 4: Build QEMU and run tests**

Run: `./tools/build_qemu.sh && python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git -C qemu add hw/misc/rcd330_vip.c
git -C qemu commit -m "hw/misc/rcd330_vip: add HANDBRAKE, variable-length RAW, and startup coding qdev props"
git add tools/run_rcd330.sh tools/tests/test_emu_plan1_vip_pss_and_shims.py
git commit -m "tools/run_rcd330: pass coding defaults to rcd330-vip for headless parity"
```

---

### Task 4: Fix `hal_gpio_shim.c` (`g_rx_stash` 1024 B, 10-Arg `log_simple` + `QEMU_LOG_TAGS`, Immediate TX `SIGIO`)

**Files:**
- Modify: `tools/arm_hal_shim/hal_gpio_shim.c:35-95,140-175,565-579`
- Modify: `tools/prebuilt/libhal_gpio_shim.so` (via `tools/build_shims.sh`)
- Modify: `tools/tests/test_emu_plan1_vip_pss_and_shims.py`

**Interfaces:**
- Consumes: `IPCL_MAX_FRAME = 1024` from `rcd330_vip.c`; `QEMU_LOG_TAGS` optional comma-separated substring list in environment (unset defaults to `"==== HMI ===="`).
- Produces:
  - `g_rx_stash[1024]` matching `IPCL_MAX_FRAME` and `libHAL` `0x404`.
  - Immediate `kill(getpid(), SIGIO)` in `msgsnd()` when an outbound `0x404`-byte IPCL frame is enqueued on `g_tx_msg_id` (eliminating up to 20 ms wait per TX message), plus shorter poll fallback (`usleep(5000)`) in `v850srq_sigio_thread` / `event0_poll_thread`.
  - 10-argument `log_simple` forwarding with `QEMU_LOG_TAGS` support (preserving exact `"==== HMI ===="` default filter when `QEMU_LOG_TAGS` is unset).

- [ ] **Step 1: Add failing tests for `hal_gpio_shim.c` fixes**

Append to `TestEmuPlan1VipPssAndShims` in `tools/tests/test_emu_plan1_vip_pss_and_shims.py`:

```python
    def test_hal_gpio_shim_rx_stash_1024_immediate_tx_sigio_and_log_tags(self):
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        self.assertIn("g_rx_stash[1024]", shim_c)
        self.assertIn("QEMU_LOG_TAGS", shim_c)
        self.assertIn("a10", shim_c)
        msgsnd_body = shim_c.split("int msgsnd(")[1].split("\n}\n")[0]
        self.assertIn("kill(getpid(), SIGIO)", msgsnd_body)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v`
Expected: FAIL (`g_rx_stash[1024]` and `QEMU_LOG_TAGS` not found).

- [ ] **Step 3: Apply fixes in `tools/arm_hal_shim/hal_gpio_shim.c` and rebuild prebuilt shims**

1. Change `static uint8_t g_rx_stash[512];` to `static uint8_t g_rx_stash[1024];`.
2. In `msgsnd()`, after setting `g_tx_pending = 1`, if `g_srq_thread_started` is set, signal immediately:
```c
        if (msqid == __atomic_load_n(&g_tx_msg_id, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&g_tx_pending, 1, __ATOMIC_RELEASE);
            if (__atomic_load_n(&g_srq_thread_started, __ATOMIC_ACQUIRE)) {
                kill(getpid(), SIGIO);
            }
        }
```
3. Reduce the idle fallback sleep in `v850srq_sigio_thread` and `event0_poll_thread` from `usleep(20000)` to `usleep(5000)` (5 ms).
4. Replace `log_simple` with 10-arg forwarding + `QEMU_LOG_TAGS` filtering:
```c
static int log_matches_filter(const char *tag, const char *fmt) {
    static int env_inited = 0;
    static const char *env_tags = NULL;
    if (!env_inited) {
        env_tags = getenv("QEMU_LOG_TAGS");
        env_inited = 1;
    }
    if (!fmt) return 0;
    if (!env_tags || !env_tags[0]) {
        return strstr(fmt, "==== HMI ====") != NULL &&
               !strstr(fmt, "BTsubsystemNotify") &&
               !strstr(fmt, "PA_OUT_DEV_STS") &&
               !strstr(fmt, "PA_OUT_VRM_INTO") &&
               !strstr(fmt, "PA_OUT_WAKE_UP_REASON") &&
               !strstr(fmt, "PA_OUT_MODE_CHG");
    }
    if (strcmp(env_tags, "*") == 0) return 1;
    const char *p = env_tags;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0 && len < 64) {
            char tok[64];
            memcpy(tok, p, len);
            tok[len] = '\0';
            if ((tag && strstr(tag, tok)) || strstr(fmt, tok)) {
                return 1;
            }
        }
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

void log_simple(const char *tag, const char *fmt,
                unsigned long a1, unsigned long a2, unsigned long a3, unsigned long a4,
                unsigned long a5, unsigned long a6, unsigned long a7, unsigned long a8,
                unsigned long a9, unsigned long a10) {
    static void (*real_log)(const char *, const char *,
                            unsigned long, unsigned long, unsigned long, unsigned long,
                            unsigned long, unsigned long, unsigned long, unsigned long,
                            unsigned long, unsigned long) = NULL;
    if (!real_log) real_log = dlsym(RTLD_NEXT, "log_simple");
    if (log_matches_filter(tag, fmt)) {
        fprintf(stderr, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
        fputc('\n', stderr);
    }
    if (real_log) real_log(tag, fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}
```
5. Rebuild shims and update `tools/prebuilt/`:
   Run: `./tools/build_shims.sh --force --update-prebuilt`

- [ ] **Step 4: Run tests to verify**

Run: `python3 tools/tests/test_emu_plan1_vip_pss_and_shims.py -v && python3 -m unittest discover -s tools/tests -p "test_plan*.py" -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/arm_hal_shim/hal_gpio_shim.c tools/prebuilt/libhal_gpio_shim.so tools/tests/test_emu_plan1_vip_pss_and_shims.py
git commit -m "tools/arm_hal_shim: 1024B SPI RX stash, immediate TX SIGIO, 10-arg log_simple with QEMU_LOG_TAGS"
```

---

### Task 5: Expose Handbrake & CAN Language Controls in Native Qt6 Control Panel

**Files:**
- Modify: `tools/rcd330_gui/src/vehicle_tab.h`
- Modify: `tools/rcd330_gui/src/vehicle_tab.cpp:155-205,301-335`
- Modify: `tools/rcd330_gui/tests/test_rcd330_gui.cpp`

**Interfaces:**
- Consumes: `HANDBRAKE <0|1>` and `LANG <valid> <code>` commands added to `rcd330_vip.c` in Tasks 2–3.
- Produces:
  - Checkable `Handbrake (Park)` button (`handbrakeBtn`) emitting `HANDBRAKE 1` / `HANDBRAKE 0` (reset to `0` on `resetForReboot()`).
  - `CAN Language` combo (`canLangCombo`) emitting `LANG 0 0` (Auto/PSS), `LANG 1 0` (EN), `LANG 1 9` (ZH), `LANG 1 6` (PT/DE), `LANG 1 13` (RU), `LANG 1 5` (ES).

- [ ] **Step 1: Add failing Qt6 unit test in `tools/rcd330_gui/tests/test_rcd330_gui.cpp`**

Add a test verifying that toggling `handbrakeBtn` emits `"HANDBRAKE 1"` then `"HANDBRAKE 0"`, and selecting an entry in `canLangCombo` emits `"LANG 1 ..."`.

```cpp
void TestRcd330Gui::testHandbrakeAndCanLanguageControls() {
    VehicleTab tab;
    QStringList sent;
    QObject::connect(&tab, &VehicleTab::commandRequested, [&](const QString &c) {
        sent.append(c);
    });
    auto *hb = tab.findChild<QPushButton *>("handbrakeBtn");
    QVERIFY(hb != nullptr);
    hb->click();
    QCOMPARE(sent.last(), QString("HANDBRAKE 1"));
    hb->click();
    QCOMPARE(sent.last(), QString("HANDBRAKE 0"));

    auto *lang = tab.findChild<QComboBox *>("canLangCombo");
    QVERIFY(lang != nullptr);
    lang->setCurrentIndex(1);
    QVERIFY(sent.last().startsWith("LANG 1 "));
}
```

- [ ] **Step 2: Run GUI test to verify it fails**

Run: `./tools/build_gui.sh --test`
Expected: FAIL (`handbrakeBtn` / `canLangCombo` missing).

- [ ] **Step 3: Add `handbrakeBtn` and `canLangCombo` to `VehicleTab`**

1. In `tools/rcd330_gui/src/vehicle_tab.h`, add `QPushButton *m_handbrakeBtn = nullptr;` and `QComboBox *m_canLangCombo = nullptr;` (include `<QComboBox>`).
2. In `tools/rcd330_gui/src/vehicle_tab.cpp`, add a "Handbrake / Lang" row under "Doors Open":
```cpp
    canGrid->addWidget(new QLabel("Handbrake / Lang", canBox), row, 0);
    auto *hbLangRow = new QHBoxLayout();
    m_handbrakeBtn = new QPushButton("Handbrake: OFF", canBox);
    m_handbrakeBtn->setObjectName("handbrakeBtn");
    m_handbrakeBtn->setCheckable(true);
    connect(m_handbrakeBtn, &QPushButton::toggled, this, [this](bool on) {
        m_handbrakeBtn->setText(on ? "Handbrake: ON" : "Handbrake: OFF");
        emit commandRequested(on ? "HANDBRAKE 1" : "HANDBRAKE 0");
    });
    m_canLangCombo = new QComboBox(canBox);
    m_canLangCombo->setObjectName("canLangCombo");
    m_canLangCombo->addItem("CAN Lang: Auto (PSS)", "LANG 0 0");
    m_canLangCombo->addItem("EN (0x00)", "LANG 1 0");
    m_canLangCombo->addItem("ZH (0x09)", "LANG 1 9");
    m_canLangCombo->addItem("PT/DE (0x06)", "LANG 1 6");
    m_canLangCombo->addItem("RU (0x0D)", "LANG 1 13");
    m_canLangCombo->addItem("ES (0x05)", "LANG 1 5");
    connect(m_canLangCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (idx >= 0) {
                    emit commandRequested(m_canLangCombo->itemData(idx).toString());
                }
            });
    hbLangRow->addWidget(m_handbrakeBtn);
    hbLangRow->addWidget(m_canLangCombo, 1);
    canGrid->addLayout(hbLangRow, row++, 1, 1, 2);
```
3. Reset `m_handbrakeBtn` (unchecked) and `m_canLangCombo` (index 0) inside `VehicleTab::resetForReboot()` using `QSignalBlocker`.

- [ ] **Step 4: Build and run GUI tests + Python test suite**

Run: `./tools/build_gui.sh --test && python3 -m unittest discover -s tools/tests -p "test_emu_plan1*.py" -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/rcd330_gui/src/vehicle_tab.h tools/rcd330_gui/src/vehicle_tab.cpp tools/rcd330_gui/tests/test_rcd330_gui.cpp
git commit -m "tools/rcd330_gui: add Handbrake (IPCL 0x06:0x03) and CAN Language controls"
```
