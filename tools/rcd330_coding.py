#!/usr/bin/env python3
"""
VW RCD 330 Headunit Coding Model, PSS Generator & Persistence Helper.

Shared by tools/rcd330_gui (native UI & live VIP sync) and
tools/build_rootfs.sh (offline /pss1 & /pss2 pre-seeding).

Reference: docs/coding.md (verified against vipImage.bin + Linux userspace).
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import struct
import sys
from typing import Any

PLATFORM_PQ = 1
PLATFORM_MQB = 2
BRAND_SKODA = 1
BRAND_VW = 2
CAN_HS = 1
CAN_LS = 2

OPS_MODES: dict[str, tuple[int, int, int, int]] = {
    "OFF": (0, 0, 0, 0),
    "4K": (0, 4, 0, 0),
    "8K": (4, 4, 0, 0),
    "360": (4, 4, 4, 4),
}

PQ_VEHICLES: dict[int, str] = {
    1: "VW321",
    2: "VW310",
    3: "VW351",
    4: "VW350",
    5: "VW411",
    6: "SK350 (Skoda)",
    7: "SK316 (Skoda)",
    8: "VW351 (Alt)",
    9: "VW321 (Alt)",
    10: "VW361",
    11: "VW463",
    12: "VW320",
    13: "VW218",
    14: "VW210",
    15: "VW250",
    16: "VW251",
    17: "VW361 (Alt)",
    18: "VN417",
    19: "VW370 (Golf/Jetta)",
    20: "SK351 (Skoda Octavia)",
    21: "SK260 (Skoda)",
    22: "SK251 (Skoda)",
    23: "VW251 (Alt)",
    24: "VW230",
    25: "VW231",
    26: "VW237",
}

MQB_VEHICLES: dict[int, str] = {
    0x32: "VW313",
    0x33: "VW326",
    0x34: "VW378",
    0x35: "SK371 (Skoda)",
    0x36: "SK326 (Skoda)",
    0x37: "SK481 (Skoda)",
    0x38: "VW370 (Golf 7)",
    0x39: "VW481",
    0x3A: "VW370 (Alt)",
    0x3E: "VW372",
    0x43: "SK326 (Alt)",
    0x44: "VW270",
    0x45: "VW271",
    0x46: "VW371",
    0x47: "VW276",
    0x48: "VW331",
    0x4B: "SK372 (Skoda)",
    0x4C: "VW483",
    0x4D: "VW421",
    0x4E: "VW331 (Alt)",
    0x4F: "VW330",
    0x50: "VW316",
    0x51: "VW326 (Alt)",
    0x52: "VW216",
}

_LOGO_VARIANTS = [
    "Normal VW",
    "Twin Drive",
    "GTD",
    "GTI",
    "BlueMotion",
    "e-Golf",
    "R-Line",
    "R",
    "Alltrack",
    "GTE",
    "Beats Audio",
    "Dynaudio",
]
_LOGO_REGIONS = ["ASIA", "NAR", "EU"]
MQB_BOOTLOGOS: dict[int, str] = {
    r * 12 + v: f"{region} — {name}"
    for r, region in enumerate(_LOGO_REGIONS)
    for v, name in enumerate(_LOGO_VARIANTS)
}

# Valid under vipImage.bin 0x1f48a:
# B0=0E (MLink+CarPlay+AA), B1=00 (int amp), B2=04 (4 ch), B3=40 (BT ON, RVC ON),
# B4=04 (F+R tweeters), B5=00, B6=13 (VW370) / 14 (SK351)
DEFAULT_PQ_CODING_VW = "0E000440040013"
DEFAULT_PQ_CODING_SKODA = "0E000440040014"
DEFAULT_MQB_CODING = "027300000000000000000000000000002F0018100000000000"


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF) matching opt/pss 0x10ac0."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_pss_block(
    payload: bytes,
    data_len: int = 25,
    client_tag: int = 0,
    counter: int = 1,
) -> bytes:
    """Build a valid 8-byte BE header + data_len payload .pss binary block."""
    padded = payload[:data_len].ljust(data_len, b"\x00")
    crc = crc16_ccitt_false(padded)
    header = struct.pack(">HHBHB", crc, data_len, client_tag & 0xFF, counter & 0xFFFF, 0)
    return header + padded


def validate_pq_coding(raw: bytes) -> list[str]:
    """Validate 7-byte PQ coding against vipImage.bin 0x1f48a exact rules."""
    errors: list[str] = []
    if len(raw) != 7:
        return [f"PQ coding must be 7 bytes (got {len(raw)})"]
    b0, b1, b2, b3, b4, b5, b6 = raw
    if sum(raw) == 0:
        errors.append("All-zero coding is rejected by VIP (DTC 01042)")
    if b0 & 0xE0:
        errors.append(f"Byte 0 (0x{b0:02X}): bits 7:5 must be 0")
    if b1 not in (0x00, 0x02):
        errors.append(f"Byte 1 (0x{b1:02X}): must be 0x00 (internal amp) or 0x02 (external SoundSystem)")
    elif b1 == 0x02:
        if b2 != 0x00:
            errors.append(f"Byte 2 (0x{b2:02X}): must be 0x00 when Byte 1 is 0x02 (SoundSystem)")
        if b4 > 0x01:
            errors.append(f"Byte 4 (0x{b4:02X}): must be 0x00..0x01 when Byte 1 is 0x02")
    else:
        if b2 not in (0x02, 0x04):
            errors.append(f"Byte 2 (0x{b2:02X}): must be 0x02 (2 spk) or 0x04 (4 spk) when Byte 1 is 0x00")
        elif b2 == 0x02 and b4 > 0x02:
            errors.append(f"Byte 4 (0x{b4:02X}): must be 0x00..0x02 when Byte 2 is 0x02")
        elif b2 == 0x04 and b4 > 0x04:
            errors.append(f"Byte 4 (0x{b4:02X}): must be 0x00..0x04 when Byte 2 is 0x04")
    if b3 & 0x9D:
        errors.append(f"Byte 3 (0x{b3:02X}): only bits 1 (ext mic), 5 (BT disable), 6 (RVC) allowed (NRC 0x31)")
    if b5 != 0x00:
        errors.append(f"Byte 5 (0x{b5:02X}): must be 0x00 (NRC 0x31)")
    if b6 >= 0x40:
        errors.append(f"Byte 6 (0x{b6:02X}): must be 0x00..0x3F")
    return errors


def default_coding_config(fw_id: str = "") -> dict[str, Any]:
    is_skoda = "SKODA" in fw_id.upper()
    return {
        "platform": "PQ",
        "brand": "SKODA" if is_skoda else "VW",
        "can_speed": "HS",
        "part_number": "6RD035187B ",
        "vehicle_mqb": 0x35 if is_skoda else 0x38,
        "speakers": 4,
        "ops_mode": "8K",
        "eng_mode": False,
        "coding_hex": DEFAULT_PQ_CODING_SKODA if is_skoda else DEFAULT_PQ_CODING_VW,
    }


def _clean_hex(hex_str: str, expected_bytes: int, fallback_hex: str) -> str:
    cleaned = re.sub(r"[^0-9A-Fa-f]", "", str(hex_str or "")).upper()
    if not cleaned or len(cleaned) % 2 != 0:
        cleaned = fallback_hex
    raw = bytes.fromhex(cleaned)
    if len(raw) < expected_bytes:
        raw = raw.ljust(expected_bytes, b"\x00")
    elif len(raw) > expected_bytes:
        raw = raw[:expected_bytes]
    return raw.hex().upper()


def normalize_coding_config(cfg: dict[str, Any] | None, fw_id: str = "") -> dict[str, Any]:
    base = default_coding_config(fw_id)
    if not isinstance(cfg, dict):
        return base

    plat = str(cfg.get("platform", base["platform"])).upper()
    if plat not in ("PQ", "MQB"):
        plat = "PQ"

    brand = str(cfg.get("brand", base["brand"])).upper()
    if brand not in ("VW", "SKODA"):
        brand = base["brand"]

    can_speed = str(cfg.get("can_speed", base["can_speed"])).upper()
    if can_speed not in ("HS", "LS"):
        can_speed = "HS"

    part_raw = str(cfg.get("part_number", base["part_number"]))
    part_ascii = "".join(ch if 32 <= ord(ch) < 127 else " " for ch in part_raw)
    part_number = part_ascii[:11].ljust(11, " ")

    try:
        vehicle_mqb = int(cfg.get("vehicle_mqb", base["vehicle_mqb"]))
    except (TypeError, ValueError):
        vehicle_mqb = base["vehicle_mqb"]
    if vehicle_mqb not in MQB_VEHICLES:
        vehicle_mqb = 0x38

    try:
        speakers = int(cfg.get("speakers", base["speakers"]))
    except (TypeError, ValueError):
        speakers = 4
    speakers = 2 if speakers == 2 else 4

    ops_mode = str(cfg.get("ops_mode", base["ops_mode"])).upper()
    if ops_mode not in OPS_MODES:
        ops_mode = "8K"

    eng_mode = bool(cfg.get("eng_mode", base["eng_mode"]))

    expected_bytes = 7 if plat == "PQ" else 25
    fallback_hex = (
        (DEFAULT_PQ_CODING_SKODA if brand == "SKODA" else DEFAULT_PQ_CODING_VW)
        if plat == "PQ"
        else DEFAULT_MQB_CODING
    )
    coding_hex = _clean_hex(cfg.get("coding_hex", fallback_hex), expected_bytes, fallback_hex)

    # On PQ, keep speakers consistent with Byte 2 if internal amp (b1 == 0)
    if plat == "PQ":
        raw = bytes.fromhex(coding_hex)
        if raw[1] == 0x00 and raw[2] in (0x02, 0x04):
            speakers = raw[2]

    return {
        "platform": plat,
        "brand": brand,
        "can_speed": can_speed,
        "part_number": part_number,
        "vehicle_mqb": vehicle_mqb,
        "speakers": speakers,
        "ops_mode": ops_mode,
        "eng_mode": eng_mode,
        "coding_hex": coding_hex,
    }


def decode_coding_fields(cfg: dict[str, Any]) -> dict[str, Any]:
    norm = normalize_coding_config(cfg)
    raw = bytes.fromhex(norm["coding_hex"])
    if norm["platform"] == "PQ":
        b0, b1, b2, b3, b4, _b5, b6 = raw
        return {
            "bt_enabled": (b3 & 0x20) == 0,
            "rvc_enabled": (b3 & 0x40) != 0,
            "ext_mic_enabled": (b3 & 0x02) != 0,
            "ext_amp_enabled": b1 == 0x02,
            "treble_mon": b4,
            "mirrorlink_enabled": (b0 & 0x02) != 0,
            "carplay_enabled": (b0 & 0x04) != 0,
            "android_auto_enabled": (b0 & 0x08) != 0,
            "carlife_enabled": (b0 & 0x10) != 0,
            "vehicle_pq": b6 & 0x3F,
            "am_disabled": False,
            "bt_coding_byte": 0x2F if (b3 & 0x20) == 0 else 0x00,
            "bootlogo_id": 0x18,
            "driving_lock": False,
            "driving_lock_handbrake": False,
            "validation_warnings": validate_pq_coding(raw),
        }
    b14 = raw[14]
    b16 = raw[16]
    b18 = raw[18]
    b19 = raw[19]
    b23 = raw[23]
    return {
        "bt_enabled": (b16 & 0x01) != 0,
        "rvc_enabled": (b19 & 0x10) != 0,
        "ext_mic_enabled": False,
        "ext_amp_enabled": False,
        "treble_mon": 0,
        "mirrorlink_enabled": True,
        "carplay_enabled": True,
        "android_auto_enabled": True,
        "carlife_enabled": False,
        "vehicle_pq": raw[6] & 0x3F,
        "am_disabled": (b14 & 0x02) != 0,
        "bt_coding_byte": b16,
        "bootlogo_id": b18,
        "driving_lock": (b23 & 0x01) != 0,
        "driving_lock_handbrake": (b23 & 0x04) != 0,
        "validation_warnings": [],
    }


def apply_coding_update(current_cfg: dict[str, Any], update: dict[str, Any], fw_id: str = "") -> dict[str, Any]:
    """Merge a UI/API update (either raw coding_hex or structured fields) into a normalized config."""
    prev = normalize_coding_config(current_cfg, fw_id)
    merged = dict(prev)

    for key in ("platform", "brand", "can_speed", "part_number", "vehicle_mqb", "speakers", "ops_mode", "eng_mode"):
        if key in update:
            merged[key] = update[key]

    new_plat = str(merged.get("platform", "PQ")).upper()
    if new_plat not in ("PQ", "MQB"):
        new_plat = "PQ"
    merged["platform"] = new_plat

    if "coding_hex" in update and update["coding_hex"] is not None:
        merged["coding_hex"] = update["coding_hex"]
    elif new_plat != prev["platform"]:
        old_fields = decode_coding_fields(prev)
        merged["coding_hex"] = (
            (DEFAULT_PQ_CODING_SKODA if merged["brand"] == "SKODA" else DEFAULT_PQ_CODING_VW)
            if new_plat == "PQ"
            else DEFAULT_MQB_CODING
        )
        update = dict(update)
        update.setdefault("bt_enabled", old_fields["bt_enabled"])
        update.setdefault("rvc_enabled", old_fields["rvc_enabled"])

    norm = normalize_coding_config(merged, fw_id)
    buf = bytearray(bytes.fromhex(norm["coding_hex"]))

    if norm["platform"] == "PQ":
        if "mirrorlink_enabled" in update:
            buf[0] = (buf[0] | 0x02) if update["mirrorlink_enabled"] else (buf[0] & ~0x02)
        if "carplay_enabled" in update:
            buf[0] = (buf[0] | 0x04) if update["carplay_enabled"] else (buf[0] & ~0x04)
        if "android_auto_enabled" in update:
            buf[0] = (buf[0] | 0x08) if update["android_auto_enabled"] else (buf[0] & ~0x08)
        if "carlife_enabled" in update:
            buf[0] = (buf[0] | 0x10) if update["carlife_enabled"] else (buf[0] & ~0x10)
        if "ext_amp_enabled" in update:
            if update["ext_amp_enabled"]:
                buf[1] = 0x02
                buf[2] = 0x00
                if buf[4] > 0x01:
                    buf[4] = 0x01
            else:
                buf[1] = 0x00
                spk = 2 if int(update.get("speakers", norm["speakers"])) == 2 else 4
                buf[2] = spk
                if buf[4] > spk:
                    buf[4] = spk
        elif "speakers" in update and buf[1] == 0x00:
            spk = 2 if int(update["speakers"]) == 2 else 4
            buf[2] = spk
            if buf[4] > spk:
                buf[4] = spk
        if "treble_mon" in update:
            max_tr = 1 if buf[1] == 0x02 else (2 if buf[2] == 0x02 else 4)
            buf[4] = max(0, min(max_tr, int(update["treble_mon"])))
        if "ext_mic_enabled" in update:
            buf[3] = (buf[3] | 0x02) if update["ext_mic_enabled"] else (buf[3] & ~0x02)
        if "bt_enabled" in update:
            # PQ Byte 3 bit 5 is INVERTED: 0 = BT enabled, 1 = BT disabled
            buf[3] = (buf[3] & ~0x20) if update["bt_enabled"] else (buf[3] | 0x20)
        if "rvc_enabled" in update:
            buf[3] = (buf[3] | 0x40) if update["rvc_enabled"] else (buf[3] & ~0x40)
        if "vehicle_pq" in update:
            veh = max(0, min(26, int(update["vehicle_pq"])))
            buf[6] = (buf[6] & 0xC0) | (veh & 0x3F)
        # Re-sync speaker count from B2 when internal amp
        if buf[1] == 0x00 and buf[2] in (0x02, 0x04):
            norm["speakers"] = buf[2]
    else:
        if "am_disabled" in update:
            buf[14] = (buf[14] | 0x02) if update["am_disabled"] else (buf[14] & ~0x02)
        if "bt_coding_byte" in update:
            buf[16] = int(update["bt_coding_byte"]) & 0xFF
        if "bt_enabled" in update:
            if update["bt_enabled"]:
                buf[16] = (buf[16] | 0x0F) if (buf[16] & 0x0F) == 0 else (buf[16] | 0x01)
            else:
                buf[16] &= ~0x01
        if "bootlogo_id" in update:
            buf[18] = max(0, min(0x23, int(update["bootlogo_id"])))
        if "rvc_enabled" in update:
            buf[19] = (buf[19] | 0x10) if update["rvc_enabled"] else (buf[19] & ~0x10)
        if "driving_lock" in update:
            buf[23] = (buf[23] | 0x01) if update["driving_lock"] else (buf[23] & ~0x01)
        if "driving_lock_handbrake" in update:
            buf[23] = (buf[23] | 0x04) if update["driving_lock_handbrake"] else (buf[23] & ~0x04)

    norm["coding_hex"] = bytes(buf).hex().upper()
    return norm


def load_coding_config(path: str | pathlib.Path | None, fw_id: str = "") -> dict[str, Any]:
    if not path:
        return default_coding_config(fw_id)
    p = pathlib.Path(path)
    if not p.is_file():
        return default_coding_config(fw_id)
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
        return normalize_coding_config(data, fw_id)
    except (OSError, json.JSONDecodeError):
        return default_coding_config(fw_id)


def save_coding_config(path: str | pathlib.Path, cfg: dict[str, Any], fw_id: str = "") -> dict[str, Any]:
    norm = normalize_coding_config(cfg, fw_id)
    p = pathlib.Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(p.suffix + ".tmp")
    tmp.write_text(json.dumps(norm, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, p)
    return norm


def vip_commands_for_coding(cfg: dict[str, Any], apply_live: bool = True) -> list[str]:
    norm = normalize_coding_config(cfg)
    plat_id = PLATFORM_PQ if norm["platform"] == "PQ" else PLATFORM_MQB
    brand_id = BRAND_SKODA if norm["brand"] == "SKODA" else BRAND_VW
    can_id = CAN_HS if norm["can_speed"] == "HS" else CAN_LS
    veh_byte = (bytes.fromhex(norm["coding_hex"])[6] & 0x3F) if plat_id == PLATFORM_PQ else norm["vehicle_mqb"]
    part_token = norm["part_number"].strip() or "6RD035187B"
    nf, nr, nright, nleft = OPS_MODES[norm["ops_mode"]]
    coding_verb = "CODING" if apply_live else "CODING_SYNC"
    eng_val = 1 if norm.get("eng_mode") else 0
    return [
        f"VRM {plat_id} {brand_id} {veh_byte} {can_id} {part_token}",
        f"OPS_CFG {nf} {nr} {nright} {nleft}",
        f"SPEAKERS {norm['speakers']}",
        f"ENGMODE {eng_val}",
        f"{coding_verb} {norm['coding_hex']}",
    ]


def _update_stdplus_ini(ini_path: pathlib.Path, cfg: dict[str, Any]) -> None:
    fields = decode_coding_fields(cfg)
    bt_val = 1 if fields["bt_enabled"] else 0
    rvc_val = 1 if fields["rvc_enabled"] else 0
    codings_mask = bt_val | (rvc_val << 1)

    sections: dict[str, list[tuple[str, str]]] = {}
    section_order: list[str] = []
    cur_sec = ""

    if ini_path.is_file():
        raw_lines = ini_path.read_text(encoding="latin-1").splitlines()
    else:
        raw_lines = []

    for line in raw_lines:
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            cur_sec = s[1:-1].strip()
            if cur_sec not in sections:
                sections[cur_sec] = []
                section_order.append(cur_sec)
        elif "=" in line and cur_sec:
            k, v = line.split("=", 1)
            sections[cur_sec].append((k.strip(), v.strip()))

    def set_kv(sec: str, key: str, value: str) -> None:
        if sec not in sections:
            sections[sec] = []
            section_order.append(sec)
        entries = sections[sec]
        for idx, (ek, _) in enumerate(entries):
            if ek == key:
                entries[idx] = (key, value)
                return
        entries.append((key, value))

    set_kv("CODING", "BT", str(bt_val))
    set_kv("CODING", "RVC", str(rvc_val))
    set_kv("CODING", "CODINGS", str(codings_mask))
    set_kv("VIP", "PLAT", cfg["platform"])
    set_kv("VIP", "BRAND", cfg["brand"])
    set_kv("VIP", "CANSPEED", cfg["can_speed"])
    set_kv("VIP", "PARTSTR", cfg["part_number"])

    out_lines: list[str] = []
    for sec in section_order:
        out_lines.append(f"[{sec}]")
        for k, v in sections[sec]:
            out_lines.append(f"{k}={v}")
    ini_path.parent.mkdir(parents=True, exist_ok=True)
    ini_path.write_text("\n".join(out_lines) + "\n", encoding="latin-1")


def seed_rootfs_coding(stage_dir: str | pathlib.Path, cfg: dict[str, Any]) -> None:
    """Pre-seed /pss1, /pss2, stdplus.ini, and bt_coding.config in a staged rootfs."""
    norm = normalize_coding_config(cfg)
    stage = pathlib.Path(stage_dir)
    pss1 = stage / "pss1"
    pss2 = stage / "pss2"
    pss1.mkdir(parents=True, exist_ok=True)
    (pss2 / "app_data").mkdir(parents=True, exist_ok=True)

    coding_raw = bytes.fromhex(norm["coding_hex"])
    pss_blob = build_pss_block(coding_raw, data_len=25, client_tag=0, counter=1)
    (pss1 / "DIAG_PSS_CODING.pss").write_bytes(pss_blob)
    (pss2 / "DIAG_PSS_CODING.pss").write_bytes(pss_blob)

    _update_stdplus_ini(pss1 / "stdplus.ini", norm)

    fields = decode_coding_fields(norm)
    bt_byte = int(fields["bt_coding_byte"]) & 0xFF
    (pss2 / "app_data" / "bt_coding.config").write_bytes(bytes([bt_byte]))


def build_coding_payload(
    cfg: dict[str, Any],
    coding_file: str | pathlib.Path = "",
    apply_live: bool = False,
) -> dict[str, Any]:
    norm = normalize_coding_config(cfg)
    return {
        "config": norm,
        "decoded": decode_coding_fields(norm),
        "vip_cmds": vip_commands_for_coding(norm, apply_live=apply_live),
        "coding_file": str(coding_file),
        "pq_vehicles": {str(k): v for k, v in PQ_VEHICLES.items()},
        "mqb_vehicles": {str(k): v for k, v in MQB_VEHICLES.items()},
        "mqb_bootlogos": {str(k): v for k, v in MQB_BOOTLOGOS.items()},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="RCD 330 Headunit Coding Utility")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_seed = sub.add_parser("seed", help="Pre-seed /pss1 & /pss2 coding files in a staged rootfs")
    p_seed.add_argument("--stage-dir", required=True)
    p_seed.add_argument("--coding-file", default="")
    p_seed.add_argument("--fw-id", default="")

    p_load = sub.add_parser("load", help="Load coding JSON and emit structured payload")
    p_load.add_argument("--coding-file", default="")
    p_load.add_argument("--fw-id", default="")

    p_apply = sub.add_parser("apply", help="Apply JSON update from stdin, persist, and emit payload")
    p_apply.add_argument("--coding-file", required=True)
    p_apply.add_argument("--fw-id", default="")
    p_apply.add_argument("--live", action="store_true")

    p_reset = sub.add_parser("reset", help="Reset coding to firmware defaults, persist, and emit payload")
    p_reset.add_argument("--coding-file", required=True)
    p_reset.add_argument("--fw-id", default="")
    p_reset.add_argument("--live", action="store_true")

    args = ap.parse_args()
    if args.cmd == "seed":
        cfg = load_coding_config(args.coding_file or None, args.fw_id)
        seed_rootfs_coding(args.stage_dir, cfg)
        return 0
    if args.cmd == "load":
        cfg = load_coding_config(args.coding_file or None, args.fw_id)
        print(json.dumps(build_coding_payload(cfg, args.coding_file, apply_live=False)))
        return 0
    if args.cmd == "apply":
        raw_in = sys.stdin.read().strip()
        try:
            update = json.loads(raw_in) if raw_in else {}
        except json.JSONDecodeError as exc:
            print(f"Invalid JSON input: {exc}", file=sys.stderr)
            return 2
        if not isinstance(update, dict):
            print("JSON object expected", file=sys.stderr)
            return 2
        cur = load_coding_config(args.coding_file, args.fw_id)
        updated = apply_coding_update(cur, update, args.fw_id)
        saved = save_coding_config(args.coding_file, updated, args.fw_id)
        print(json.dumps(build_coding_payload(saved, args.coding_file, apply_live=args.live)))
        return 0
    if args.cmd == "reset":
        cfg = default_coding_config(args.fw_id)
        saved = save_coding_config(args.coding_file, cfg, args.fw_id)
        print(json.dumps(build_coding_payload(saved, args.coding_file, apply_live=args.live)))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
