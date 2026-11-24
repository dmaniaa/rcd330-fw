#!/usr/bin/env python3
"""Builds src/hmi/TRACE.json, the index from stock VAs to recreated code.

Scans src/hmi/**/*.{cpp,h} for origin tags at the start of a line (after
indentation): `// 0x<8 lowercase hex> <name>`. Helper comments that merely
derive from a stock function carry no tag in that form and are ignored.

Each tagged VA must be either
  * a decompiled stock function (docs/decompiled/hmi/functions/0x<va>.c), or
  * a data address: inside a non-code section of the 5624 binary according to
    docs/decompiled/hmi/hmi_symbols_5624.json (.rodata, .data, .bss, ...).
Anything else (e.g. an address inside a function body, or a typo) is an error.

TRACE.json layout:
  "0x0005ff4c": {"name", "file", "line"[, "also": [{"file","line"}]]}   functions
  "_data":      same shape, keyed by data VA
  "_coverage":  {"recreated": distinct function VAs, "total": 9580}

Usage: gen_trace.py [--check] [--src DIR] [--out FILE]
  --check  do not write; fail if TRACE.json is stale or a tag is invalid.
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_SRC = ROOT / "src/hmi"
DEFAULT_OUT = ROOT / "src/hmi/TRACE.json"
DECOMPILED = ROOT / "docs/decompiled/hmi"
SYMBOLS = DECOMPILED / "hmi_symbols_5624.json"
TAG_RE = re.compile(r"^\s*//\s(0x[0-9a-f]{8})\s(\S.*)$")
CODE_SECTIONS = {".init", ".plt", ".text", ".fini"}
EXTENSIONS = {".cpp", ".h"}
# Generated/vendored trees carry no origin tags of ours.
SKIP_DIRS = {"third_party", "__pycache__"}


def load_symbols():
    return json.loads(SYMBOLS.read_text())


def data_ranges(sym=None):
    sections = (sym or load_symbols())["sections"]
    ranges = []
    for sec in sections:
        addr = int(sec["addr"], 16)
        if addr and sec["name"] not in CODE_SECTIONS:
            ranges.append((addr, addr + int(sec["size"], 16)))
    return ranges


def stock_function_vas(sym=None):
    """Set of integer start VAs for all 9,580 decompiled opt/hmi functions.

    Reads docs/decompiled/hmi/functions/0x*.c when present; falls back to
    hmi_symbols_5624.json["function_addresses"] on public checkouts that omit
    raw decompiled .c files.
    """
    functions_dir = DECOMPILED / "functions"
    if functions_dir.is_dir():
        return {int(p.stem, 16) for p in functions_dir.glob("0x*.c")}
    addrs = (sym or load_symbols()).get("function_addresses")
    if not addrs:
        raise RuntimeError(
            f"neither {functions_dir} nor function_addresses in {SYMBOLS} is available"
        )
    return {int(a, 16) for a in addrs}


def source_files(src):
    for path in sorted(src.rglob("*")):
        if path.suffix in EXTENSIONS and not SKIP_DIRS.intersection(path.relative_to(src).parts):
            yield path


def scan(src):
    """Returns [(va, name, relative file, line)] in file/line order."""
    tags = []
    for path in source_files(src):
        rel = path.relative_to(src).as_posix()
        for number, text in enumerate(path.read_text().splitlines(), 1):
            match = TAG_RE.match(text)
            if match:
                tags.append((match.group(1), match.group(2).strip(), rel, number))
    return tags


def build_index(src):
    sym = load_symbols()
    ranges = data_ranges(sym)
    func_vas = stock_function_vas(sym)
    total = len(func_vas)
    funcs, data, errors = {}, {}, []
    for va, name, rel, line in scan(src):
        value = int(va, 16)
        target = funcs if value in func_vas else None
        if target is None:
            if any(lo <= value < hi for lo, hi in ranges):
                target = data
            else:
                errors.append(f"{rel}:{line}: {va} {name}: not a decompiled function start or a data address")
                continue
        if va in target:
            target[va].setdefault("also", []).append({"file": rel, "line": line})
        else:
            target[va] = {"name": name, "file": rel, "line": line}
    index = dict(sorted(funcs.items()))
    index["_data"] = dict(sorted(data.items()))
    index["_coverage"] = {"recreated": len(funcs), "total": total}
    return index, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--src", type=pathlib.Path, default=DEFAULT_SRC)
    parser.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    index, errors = build_index(args.src)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    text = json.dumps(index, indent=2) + "\n"
    if args.check:
        current = args.out.read_text() if args.out.is_file() else None
        if current != text:
            print(f"{args.out} is stale; run src/hmi/tools/gen_trace.py", file=sys.stderr)
            return 1
        return 0
    args.out.write_text(text)
    cov = index["_coverage"]
    print(f"{args.out}: {cov['recreated']}/{cov['total']} functions, {len(index['_data'])} data addresses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
