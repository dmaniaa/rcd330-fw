"""Read-only access to a stock 32-bit little-endian ARM ELF by virtual address.

VA -> file offset comes from the PT_LOAD program headers. For stock 5624
opt/hmi that is PT_LOAD[0] (.text/.rodata): offset = va - 0x8000 and
PT_LOAD[1] (.data/.got/...): offset = va - 0x10000 (AGENTS.md section 2).
"""
import os
import pathlib
import struct

PT_LOAD = 1


class ElfError(Exception):
    pass


class Elf:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.segments = self._load_segments()

    def _load_segments(self):
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            raise ElfError(f"{self.path}: not a 32-bit little-endian ELF")
        e_phoff, = struct.unpack_from("<I", d, 0x1c)
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x2a)
        segments = []
        for i in range(e_phnum):
            p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from(
                "<IIIII", d, e_phoff + i * e_phentsize)
            if p_type == PT_LOAD:
                segments.append((p_vaddr, p_vaddr + p_filesz, p_offset))
        if not segments:
            raise ElfError(f"{self.path}: no PT_LOAD segments")
        return segments

    def offset(self, va, n=1):
        for start, end, file_off in self.segments:
            if start <= va and va + n <= end:
                return va - start + file_off
        raise ElfError(f"{self.path}: VA {va:#x}+{n:#x} is outside every PT_LOAD file image")

    def read(self, va, n):
        off = self.offset(va, n)
        return self.data[off:off + n]

    def unpack(self, fmt, va):
        return struct.unpack(fmt, self.read(va, struct.calcsize(fmt)))

    def u8(self, va):
        return self.read(va, 1)[0]

    def u16(self, va):
        return self.unpack("<H", va)[0]

    def u32(self, va):
        return self.unpack("<I", va)[0]

    def cstr(self, va):
        off = self.offset(va)
        end = self.data.find(b"\0", off)
        if end < 0:
            raise ElfError(f"{self.path}: unterminated string at VA {va:#x}")
        self.offset(va, end - off + 1)
        return self.data[off:end].decode("ascii")


# Helpers shared by the table generators (gen_igdt_tables.py, gen_strings.py).

def c_string(s):
    """UTF-8 C string literal. Non-ASCII bytes use octal escapes (\\x would
    swallow following hex digits) and so does '?' (no trigraphs)."""
    out = []
    for b in s.encode("utf-8"):
        ch = chr(b)
        if ch in '\\"':
            out.append("\\" + ch)
        elif 0x20 <= b < 0x7f and ch != "?":
            out.append(ch)
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def write_if_changed(path, text):
    """Write text (UTF-8) to path atomically unless it already holds exactly
    that, so an unchanged generated table keeps its mtime and is not
    recompiled. Returns True when the file was written."""
    out = pathlib.Path(path)
    data = text.encode("utf-8")
    if out.is_file() and out.read_bytes() == data:
        return False
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_name(out.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, out)
    return True
