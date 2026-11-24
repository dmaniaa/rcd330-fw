#!/usr/bin/env python3
"""Decode the stock compressed multi-language string block into C++ tables.

Follows FUN_00023650 (0x23650) and its caller FUN_0031d1bc (0x31d1bc); format
in docs/assets-and-localization.md section 2. Writes
build/hmi/gen/strings_vw.cpp for src/hmi/display/igdt/igdt_strings.h.

decode_block() returns the same shape as resources/translations/
strings_5624_vw.json (minus hmi_path).
"""
import argparse
import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hmi_elf  # noqa: E402
import gen_igdt_tables  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_OUT = ROOT / "build/hmi/gen/strings_vw.cpp"
STOCK_SHA256 = gen_igdt_tables.STOCK_SHA256

BLOCK_VA = 0x3cb680             # file 0x3c3680 (PT_LOAD[0]: file = va - 0x8000)
BLOCK_FILE_OFFSET = 0x3c3680
MAX_ID = 0x20fcb                # FUN_00023650 rejects ids >= 0x20fcc; last group starts here
MULTILANG_START = 0x2d2         # first 5-language group; ids <= 0x2cf are language-neutral
NEUTRAL_START = 9               # bytes 0..8 are nine empty chunks (ids 0..8 decode to "")
LANGUAGES = ["en_US", "zh_CN", "pt_BR", "ru_RU", "es_MX"]
ENG_TABLE_VA = 0x8a4c00         # UTF-32LE engineering strings, NUL separated
ENG_WORD_LIMIT = 0x110000       # first word above this ends the table (a pointer follows)

TAG_BACKREF = 0x1f
FLAG_CONT = 0x80
FLAG_LONG_REF = 0x20


class StringError(Exception):
    pass


def chunk_end(block, pos):
    """Byte offset just past the chunk at pos; (end, is_backref)."""
    b0 = block[pos]
    tag = b0 & 0x1f
    if tag == TAG_BACKREF:
        return pos + (4 if b0 & FLAG_LONG_REF else 3), True
    return pos + 1 + tag, False


def string_end(block, pos):
    """Offset just past the string starting at pos (the skip loop of 0x23650)."""
    while True:
        if pos + 4 > len(block):
            raise StringError("string at offset %#x runs past the block" % pos)
        b0 = block[pos]
        pos, _ = chunk_end(block, pos)
        if not b0 & FLAG_CONT:
            return pos


def decode_chunks(block, pos):
    """Decode one string; returns (text, backref target or None)."""
    chars = []
    while True:
        if pos + 4 > len(block):
            raise StringError("string at offset %#x runs past the block" % pos)
        b0 = block[pos]
        tag = b0 & 0x1f
        if tag == TAG_BACKREF:
            target = (block[pos + 1] << 8) | block[pos + 2]
            if b0 & FLAG_LONG_REF:
                target = (target << 8) | block[pos + 3]
            return "", target
        mode = (b0 >> 5) & 3
        if mode == 3:
            page, data, count = block[pos + 1], pos + 2, tag - 1
        else:
            page, data, count = (0, 1, 4)[mode], pos + 1, tag
        for b in block[data:data + count]:
            cp = (page << 8) | b
            if 0xd800 <= cp <= 0xdfff:
                raise StringError("surrogate code point %#06x in string at %#x" % (cp, pos))
            chars.append(chr(cp))
        pos += 1 + tag
        if not b0 & FLAG_CONT:
            return "".join(chars), None


def decode_string(block, sid, lang):
    """FUN_00023650 + FUN_0031d1bc: string sid, language slot lang (0 for neutral)."""
    if sid > MAX_ID:
        raise StringError("string id %#x is outside the block (max %#x)" % (sid, MAX_ID))
    if sid > 0x2cf:
        for _ in range(lang):
            sid = string_end(block, sid)
    text, target = decode_chunks(block, sid)
    if target is not None:
        if target > MAX_ID:
            raise StringError("back-reference to %#x is outside the block" % target)
        text, again = decode_chunks(block, target)     # 0x31d1bc follows one level, lang 0
        if again is not None:
            raise StringError("back-reference chain at %#x" % target)
    return text


def decode_engineering(elf):
    words = []
    for i in range(0x10000):
        w = elf.u32(ENG_TABLE_VA + 4 * i)
        if w >= ENG_WORD_LIMIT:
            break
        words.append(w)
    else:
        raise StringError("engineering string table has no end")
    entries, start = [], 0
    for i, w in enumerate(words):
        if w == 0:
            entries.append({"offset_wchars": "0x%04x" % start,
                            "text": "".join(map(chr, words[start:i]))})
            start = i + 1
    if start != len(words):
        raise StringError("engineering string table ends without a NUL terminator (text at wchar %#x)" % start)
    return entries


def decode_block(elf):
    block = elf.read(BLOCK_VA, MAX_ID + 1 + 0x40)       # the last group runs past MAX_ID
    neutral, multilang = {}, {}
    pos = NEUTRAL_START
    while pos < MULTILANG_START:
        neutral["0x%04x" % pos] = decode_string(block, pos, 0)
        pos = string_end(block, pos)
    if pos != MULTILANG_START:
        raise StringError("neutral strings end at %#x, expected %#x" % (pos, MULTILANG_START))
    while pos <= MAX_ID:
        multilang["0x%04x" % pos] = {
            lang: decode_string(block, pos, i) for i, lang in enumerate(LANGUAGES)}
        for _ in LANGUAGES:
            pos = string_end(block, pos)
    engineering = decode_engineering(elf)
    return {
        "block_file_offset": "0x%x" % BLOCK_FILE_OFFSET,
        "block_length": "0x%x" % pos,
        "multilang_start_offset": "0x%x" % MULTILANG_START,
        "languages": list(LANGUAGES),
        "neutral_count": len(neutral),
        "multilang_count": len(multilang),
        "utf32_count": len(engineering),
        "neutral": neutral,
        "multilang": multilang,
        "utf32_engineering": engineering,
    }


def emit_cpp(decoded):
    lines = [
        "// File name is ours; stock name unknown.",
        "// Generated by src/hmi/tools/gen_strings.py from stock opt/hmi",
        "// (firmware 5624, sha256 %s). Do not edit." % STOCK_SHA256,
        '#include "display/igdt/igdt_strings.h"',
        "",
        "namespace {",
        "",
        "// 0x%08x string block (decoded), ids = block byte offsets, ascending" % BLOCK_VA,
        "const IgdtStringEntry kStrings[] = {",
    ]
    for sid, text in decoded["neutral"].items():
        lines.append("    {%su, {%s, \"\", \"\", \"\", \"\"}}," % (sid, hmi_elf.c_string(text)))
    for sid, texts in decoded["multilang"].items():
        row = ", ".join(hmi_elf.c_string(texts[lang]) for lang in LANGUAGES)
        lines.append("    {%su, {%s}}," % (sid, row))
    lines += [
        "};",
        "",
        "// 0x%08x UTF-32 engineering strings, ids = wchar offsets, ascending" % ENG_TABLE_VA,
        "const IgdtEngString kEngStrings[] = {",
    ]
    for e in decoded["utf32_engineering"]:
        lines.append("    {%su, %s}," % (e["offset_wchars"], hmi_elf.c_string(e["text"])))
    lines += [
        "};",
        "",
        "} // namespace",
        "",
        "const IgdtStringTables g_igdtStringsVw = {",
        "    kStrings, %d, kEngStrings, %d" % (
            len(decoded["neutral"]) + len(decoded["multilang"]), len(decoded["utf32_engineering"])),
        "};",
        "",
    ]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--hmi", default=str(gen_igdt_tables.DEFAULT_HMI))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    args = ap.parse_args(argv)
    try:
        elf = gen_igdt_tables.open_stock_hmi(args.hmi)
        source = emit_cpp(decode_block(elf))
    except (gen_igdt_tables.GenError, hmi_elf.ElfError, StringError) as exc:
        print("Error: %s" % exc, file=sys.stderr)
        return 1
    if hmi_elf.write_if_changed(args.out, source):
        print("Wrote %s" % args.out)
    else:
        print("Unchanged %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
