#!/usr/bin/env python3
"""Generate the IGDT scene/texture C++ tables from stock 5624 opt/hmi .rodata.

Reads scr_table, the 12-byte scene node graph, the static/dynamic image and
text records, text_layout and tex_table (docs/hmi-internals.md section 6,
docs/assets-and-localization.md section 1) and writes
build/hmi/gen/igdt_tables_vw.cpp for src/hmi/display/igdt/igdt_tables.h.

Each screen's elements are flattened in scene-walk draw order with every
branch/state node resolved to its initial child, exactly as walked by
FUN_0032072c -> FUN_0031f8e4 / FUN_003202c4 / FUN_0031f0a0 / FUN_0031f41c.
resources/scenes/scenes_5624_vw.json came from an earlier extractor with known
defects; tools/tests/test_hmi_tables.py lists where this output differs.
"""
import argparse
import hashlib
import os
import pathlib
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hmi_elf  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_HMI = ROOT / "extracted-fw/5624_vw/opt/hmi"
DEFAULT_OUT = ROOT / "build/hmi/gen/igdt_tables_vw.cpp"
STOCK_SHA256 = "44ccad4211fc8f2fb21f2db46a04271155b8d894735427802baee7467630a6f2"

# Table VAs and strides (5624_vw), docs/hmi-internals.md section 6.
SCR_TABLE = 0x4536a4        # 16 B: u8 layer_group, u8, u8 root_count, u8, u32, u32, u32 root node VA
SCR_COUNT = 255
TEX_TABLE = 0x69b69c        # 24 B <BBHhhIHHII>
TEX_TABLE_END = 0x7578ec
TEX_STRIDE = 24
NODE_STRIDE = 12            # <u16 eidx, u16 ccnt, u32 tag, u32 cptr>
BRANCH_INIT, BRANCH_COUNT = 0x78f0c4, 0x10f0   # u16 each, memcpy'd to RAM 0xc502d8 by FUN_00324328
STATE_INIT, STATE_COUNT = 0x7912a4, 0x1b11     # u16 each, memcpy'd to RAM 0xc524b8 by FUN_00324328
STATIC_IMG, DYN_IMG, IMG_STRIDE = 0x896d38, 0x51df44, 0x38
STATIC_TXT, DYN_TXT, TXT_STRIDE = 0x874b70, 0x759eec, 0x3c
TEXT_LAYOUT, TEXT_LAYOUT_STRIDE = 0x46d6a0, 0x14
TEXT_LAYOUT_COUNT = 0xe3f   # FUN_003223d4 / FUN_000615ec reject ids >= 0xe3f
STR_ID_RUNTIME = 0xffffffff  # IgdtElement.str_id of text drawn from the runtime buffer
GROUP_FLAGS = 0x454694      # 4 B per group eidx: u8 flags, u8 flow spacing, u16 clip_table index
CLIP_TABLE = 0x45cca0       # 8 B: s16 x, y, w, h (relative to the current origin)
GROUP_CLIP = 0x3            # flags: group clips its children (FUN_0031da6c)
GROUP_PLAIN = 0xc           # flags & 0xc == 0xc: plain child walk; else flow layout
STATIC_GRP, DYN_GRP, GRP_STRIDE = 0x875c14, 0x47f97c, 0x34
STATIC_RECT, RECT_STRIDE = 0x896cd0, 0x34
RECT_LAYOUT, RECT_LAYOUT_STRIDE = 0x8a4be8, 8

# Node tag: bits 24..27 = kind, bit 30 = dynamic record, bits 0..19 = record index.
KIND_GROUP, KIND_BRANCH, KIND_IMG, KIND_STATE, KIND_TXT, KIND_TXT_DYN, KIND_RECT = 0, 1, 2, 3, 4, 5, 7
TAG_DYNAMIC = 0x40000000
TAG_INDEX_MASK = 0xfffff

# The stock walker has no depth limit; the deepest 5624 scene is far below
# this, so exceeding it means a corrupt or cyclic graph.
MAX_SANE_DEPTH = 64

# Clip in effect when FUN_0032072c starts a screen: FUN_0031d684 resets the
# clip to (0, 0, 0xa0f94c, 0xa0f94e), the display size, and the origin to 0.
SCREEN_CLIP = (0, 0, 800, 480)  # x0, y0, x1, y1

# Screen names follow the stock page classes that own them
# (docs/hmi-internals.md section 6 page -> screen mapping); others are ours.
SCREEN_NAMES = {
    0x04: "AppNoConn",
    0x06: "PhoneDial",
    0x0a: "CarEntryMain",
    0x0e: "CarMFA",
    0x17: "CarSetup",
    0x4f: "MediaBrw",
    0x52: "MediaOverview",
    0x63: "PhoneMain",
    0x71: "AppConn",
    0xdf: "RVC",
    0xe6: "SoundSetupMain",
    0xee: "SystemSetupMain",
    0xf9: "TunerMain",
    0xfe: "StatusBar",
}

ELEM_IMG, ELEM_TXT, ELEM_RECT = 2, 4, 7   # IgdtElement.type


class GenError(Exception):
    pass


def screen_name(sid):
    return SCREEN_NAMES.get(sid, "Screen_0x%02x" % sid)


def load_textures(elf):
    """All tex_table entries as <BBHhhIHHII> tuples, in table order."""
    count = (TEX_TABLE_END - TEX_TABLE) // TEX_STRIDE
    return [elf.unpack("<BBHhhIHHII", TEX_TABLE + i * TEX_STRIDE) for i in range(count)]


class Element:
    """One drawable node (img or txt) with its resolved record fields."""

    def __init__(self, kind, tag, x, y, w, h, alpha, color, tex=-1, asset=None,
                 font=0, align=0, str_id=0, rt_off=0, bg_color=0):
        self.depth = 0      # scene-walk nesting level (root list = 0); not emitted
        self.clip = None    # (x0, y0, x1, y1) of enclosing clip groups, None = none
        self.kind, self.tag = kind, tag
        self.x, self.y, self.w, self.h = x, y, w, h
        self.alpha, self.color = alpha, color
        self.tex, self.asset = tex, asset
        self.font, self.align = font, align
        self.str_id = str_id
        self.rt_off = rt_off
        self.bg_color = bg_color

    def as_json(self):
        if self.kind == ELEM_IMG:
            return {"type": "img", "tag": "0x%08x" % self.tag, "x": self.x, "y": self.y,
                    "w": self.w, "h": self.h, "alpha": self.alpha,
                    "tint": "0x%08x" % self.color, "asset": self.asset}
        if self.kind == ELEM_RECT:
            return {"type": "rect", "tag": "0x%08x" % self.tag, "x": self.x, "y": self.y,
                    "w": self.w, "h": self.h, "alpha": self.alpha,
                    "color": "0x%08x" % self.color}
        return {"type": "txt", "tag": "0x%08x" % self.tag, "x": self.x, "y": self.y,
                "w": self.w, "h": self.h, "font": self.font, "align": "0x%02x" % self.align,
                "color": "0x%08x" % self.color, "alpha": self.alpha}


class SceneWalker:
    def __init__(self, elf):
        self.elf = elf
        self.textures = load_textures(elf)

    def node(self, va):
        return self.elf.unpack("<HHII", va)

    def image(self, eidx, tag):
        # 0x0031f0a0: record +0 alpha, +4 s16 x/y, +8 tint; texture = tex_table[eidx].
        base = DYN_IMG if tag & TAG_DYNAMIC else STATIC_IMG
        alpha, _, _, x, y, tint = self.elf.unpack(
            "<BBHhhI", base + (tag & TAG_INDEX_MASK) * IMG_STRIDE)
        if eidx >= len(self.textures):
            raise GenError("img node tag %#010x: tex index %d outside tex_table" % (tag, eidx))
        tex = self.textures[eidx]
        asset = self.elf.cstr(tex[9]).rsplit("/", 1)[-1]
        return Element(ELEM_IMG, tag, x, y, tex[6], tex[7], alpha, tint, tex=eidx, asset=asset)

    def text(self, eidx, tag):
        # Text record +0 alpha, +4 s16 x/y, +8 color, +0xc background color
        # (FUN_0031dffc); text_layout[eidx] +4 u16 w, +6 u8 h (really the
        # max wchar count, FUN_003223d4), +7 u8 font, +8 u8 align.
        base = DYN_TXT if tag & TAG_DYNAMIC else STATIC_TXT
        alpha, _, _, x, y, color, bg_color = self.elf.unpack(
            "<BBHhhII", base + (tag & TAG_INDEX_MASK) * TXT_STRIDE)
        w, h, font, align = self.elf.unpack("<HBBB", TEXT_LAYOUT + eidx * TEXT_LAYOUT_STRIDE + 4)
        # FUN_0031f41c: node kind 4 draws wide text at text_layout[eidx] +0 (a wchar
        # offset) in the UTF-32 engineering table (0x8a4c00); kind 5 draws it from a
        # runtime buffer (+0x2e088 of the HMI state) that page code fills with strings
        # from the 0x23650 block, so the scene tables carry no block string id for it.
        # local_58 = text_layout[eidx] +0 is the wchar offset into *(state + 0x2e088).
        offset = self.elf.u32(TEXT_LAYOUT + eidx * TEXT_LAYOUT_STRIDE)
        str_id, rt_off = STR_ID_RUNTIME, offset
        if (tag >> 24) & 0xf == 4:
            str_id, rt_off = offset, 0
        return Element(ELEM_TXT, tag, x, y, w, h, alpha, color, font=font, align=align,
                       str_id=str_id, rt_off=rt_off, bg_color=bg_color)

    def rect(self, eidx, tag):
        # 0x0031f8e4 kind 7 (static only): record at 0x896cd0 + idx*0x34
        # +0 alpha, +4 s16 x/y; rect_layout[eidx] (8 B) +0 u16 w, +2 u16 h,
        # +4 u32 color.
        alpha, _, _, x, y = self.elf.unpack("<BBHhh", STATIC_RECT + (tag & TAG_INDEX_MASK) * RECT_STRIDE)
        w, h, color = self.elf.unpack("<HHI", RECT_LAYOUT + eidx * RECT_LAYOUT_STRIDE)
        return Element(ELEM_RECT, tag, x, y, w, h, alpha, color)

    @staticmethod
    def emit(out, element, depth, clip):
        element.depth = depth
        element.clip = clip
        out.append(element)

    def check_depth(self, depth, va):
        if depth > MAX_SANE_DEPTH:
            raise GenError("scene graph deeper than %d at node VA %#x (cycle?)" % (MAX_SANE_DEPTH, va))

    # 0x0031f8e4 FUN_0031f8e4 (draw node list)
    def walk(self, va, count, depth, clip, out):
        self.check_depth(depth, va)
        for i in range(count):
            node_va = va + i * NODE_STRIDE
            eidx, ccnt, tag, cptr = self.node(node_va)
            kind = (tag >> 24) & 0xf
            if kind in (KIND_GROUP, KIND_BRANCH):
                self.group(node_va, depth + 1, clip, out)
            elif kind == KIND_IMG:
                self.emit(out, self.image(eidx, tag), depth, clip)
            elif kind == KIND_STATE:
                # Only an img child of the selected state is drawn.
                child = self.selected_child(node_va, STATE_INIT, STATE_COUNT)
                if child is not None:
                    c_eidx, _, c_tag, _ = self.node(child)
                    if (c_tag >> 24) & 0xf == KIND_IMG:
                        self.emit(out, self.image(c_eidx, c_tag), depth + 1, clip)
            elif kind in (KIND_TXT, KIND_TXT_DYN):
                self.emit(out, self.text(eidx, tag), depth, clip)
            elif kind == KIND_RECT and not tag & TAG_DYNAMIC:
                self.emit(out, self.rect(eidx, tag), depth, clip)
            # Dynamic kind 7 draws nothing; kind 8 reads runtime RAM only.

    # 0x003202c4 FUN_003202c4 (group / branch)
    def group(self, va, depth, clip, out):
        self.check_depth(depth, va)
        eidx, ccnt, tag, cptr = self.node(va)
        if (tag >> 24) & 0xf == KIND_GROUP:
            flags = self.elf.u8(GROUP_FLAGS + eidx * 4)
            if flags & GROUP_PLAIN != GROUP_PLAIN and cptr and ccnt:
                # Flow layout places children by measured width at runtime,
                # which a flattened table cannot express. 5624 has none with
                # children; fail rather than emit wrong positions.
                raise GenError("flow-layout group (flags %#04x) with children at node VA %#x, "
                               "tag %#010x: unsupported" % (flags, va, tag))
            if flags & GROUP_CLIP:
                clip = intersect_clip(clip or SCREEN_CLIP, self.group_clip_rect(eidx, tag))
            if cptr:
                self.walk(cptr, ccnt, depth, clip, out)
        elif cptr and (tag & TAG_INDEX_MASK) < BRANCH_COUNT:
            child = self.selected_child(va, BRANCH_INIT, BRANCH_COUNT)
            if child is not None:
                self.group(child, depth + 1, clip, out)

    def group_clip_rect(self, eidx, tag):
        """Group clip rect (x, y, w, h) as FUN_003202c4 passes it to FUN_0031da6c."""
        if not tag & TAG_DYNAMIC:
            return self.elf.unpack("<hhhh", CLIP_TABLE + self.elf.u16(GROUP_FLAGS + eidx * 4 + 2) * 8)
        # Dynamic groups clip with their RAM copy, which FUN_00323c48
        # initializes from dyn_grp[idx] +2 (group key): clip_table[key's
        # index], or (0, 0, -1, -1) when that index is 0.
        key = self.elf.u16(DYN_GRP + (tag & TAG_INDEX_MASK) * GRP_STRIDE + 2)
        index = self.elf.u16(GROUP_FLAGS + key * 4 + 2)
        if index == 0:
            return (0, 0, -1, -1)
        return self.elf.unpack("<hhhh", CLIP_TABLE + index * 8)

    # 0x00321ea8 FUN_00321ea8 / 0x00321f00 FUN_00321f00 (selected child)
    def selected_child(self, va, init_table, table_count):
        eidx, ccnt, _, cptr = self.node(va)
        if not cptr or eidx >= table_count:
            return None
        child = self.elf.u16(init_table + eidx * 2)
        return cptr + child * NODE_STRIDE if child < ccnt else None

    def screen(self, sid):
        layer_group, _, root_count, _, _, _, root = self.elf.unpack(
            "<BBBBIII", SCR_TABLE + sid * 16)
        elements = []
        if root:
            self.walk(root, root_count, 0, None, elements)
        return layer_group, root_count, elements


def s16(v):
    return (v + 0x8000) % 0x10000 - 0x8000


# 0x0031da6c FUN_0031da6c -> 0x0031d9cc / 0x0031d86c: rect (x, y, w, h) is
# made absolute (origin is 0 in the static tables) and intersected with the
# current clip; no overlap leaves the empty clip (0, 0, 0, 0).
def intersect_clip(clip, rect):
    cx0, cy0, cx1, cy1 = clip
    x, y, w, h = rect
    rx0, ry0, rx1, ry1 = x, y, s16(x + w), s16(y + h)
    if cx0 <= rx1 and rx0 <= cx1 and cy0 <= ry1 and ry0 <= cy1:
        return (max(cx0, rx0), max(cy0, ry0), min(cx1, rx1), min(cy1, ry1))
    return (0, 0, 0, 0)


def load_screen_elements(elf):
    """{screen id: (layer_group, root_count, [Element])} for all 255 screens."""
    walker = SceneWalker(elf)
    return {sid: walker.screen(sid) for sid in range(SCR_COUNT)}


def load_screens(elf):
    """{screen id: dict shaped like a scenes_5624_vw.json screen (no widget_boxes)}."""
    screens = {}
    for sid, (layer_group, root_count, elements) in load_screen_elements(elf).items():
        screens[sid] = {
            "screen_id": "0x%02x" % sid,
            "name": screen_name(sid),
            "layer_group": layer_group,
            "root_count": root_count,
            "element_count": len(elements),
            "elements": [e.as_json() for e in elements],
        }
    return screens


def load_text_layouts(elf):
    """(wchar offset, max wchars) of every text_layout entry FUN_003223d4
    accepts (id < TEXT_LAYOUT_COUNT)."""
    return [elf.unpack("<IHB", TEXT_LAYOUT + i * TEXT_LAYOUT_STRIDE)[0::2]
            for i in range(TEXT_LAYOUT_COUNT)]


def runtime_text_len(elf):
    """wchars of the runtime text buffer (0x00c62264): the furthest end of a
    layout whose +0x10 bit 1 selects that buffer (FUN_0031ee54)."""
    end = 0
    for i, (offset, max_chars) in enumerate(load_text_layouts(elf)):
        if elf.u8(TEXT_LAYOUT + i * TEXT_LAYOUT_STRIDE + 0x10) & 2:
            end = max(end, offset + max_chars)
    return end


def emit_cpp(elf):
    textures = load_textures(elf)
    screens = load_screen_elements(elf)
    if len(textures) > 0xffff:
        raise GenError("tex_table has %d entries; IgdtSkinTables.tex_count is uint16_t" % len(textures))
    lines = [
        "// File name is ours; stock name unknown.",
        "// Generated by src/hmi/tools/gen_igdt_tables.py from stock opt/hmi",
        "// (firmware 5624, sha256 %s). Do not edit." % STOCK_SHA256,
        '#include "display/igdt/igdt_tables.h"',
        "",
        "namespace {",
        "",
        "// 0x%08x tex_table" % TEX_TABLE,
        "const IgdtTexEntry kTextures[%d] = {" % len(textures),
    ]
    for fmt, flags, a0, a1, a2, bsz, w, h, tint, path_va in textures:
        lines.append("    {0x%02x, 0x%02x, 0x%04x, %d, %d, %du, %d, %d, 0x%08xu, %s}," % (
            fmt, flags, a0, a1, a2, bsz, w, h, tint, hmi_elf.c_string(elf.cstr(path_va))))
    lines += ["};", "", "const IgdtElement kElements[] = {"]
    screen_rows = []
    clips = []          # distinct (x0, y0, x1, y1), emitted as IgdtClipRect
    clip_index = {}
    first = 0
    for sid in range(SCR_COUNT):
        layer_group, _, elements = screens[sid]
        lines.append("    // screen 0x%02x %s" % (sid, screen_name(sid)))
        for e in elements:
            if not -1 <= e.tex < len(textures):
                raise GenError("screen 0x%02x element tex %d outside tex_table (%d entries)" % (
                    sid, e.tex, len(textures)))
            clip = -1
            if e.clip is not None:
                clip = clip_index.setdefault(e.clip, len(clips))
                if clip == len(clips):
                    clips.append(e.clip)
            lines.append("    {%d, 0x%08xu, %d, %d, %d, %d, %d, 0x%08xu, %d, %d, 0x%02x, 0x%08xu, %du, %d, 0x%08xu}," % (
                e.kind, e.tag, e.x, e.y, e.w, e.h, e.alpha, e.color, e.tex, e.font, e.align, e.str_id, e.rt_off, clip,
                e.bg_color))
        screen_rows.append("    {0x%02x, %s, %d, %d, %d}," % (
            sid, hmi_elf.c_string(screen_name(sid)), layer_group, first, len(elements)))
        first += len(elements)
    if first > 0xffff:
        raise GenError("%d elements; IgdtScreen.elem_first is uint16_t" % first)
    if len(clips) > 0x7fff:
        raise GenError("%d clip rects; IgdtElement.clip is int16_t" % len(clips))
    lines += ["};", "", "// Accumulated group clips (0x0031da6c), x/y/w/h in screen pixels.",
              "const IgdtClipRect kClips[%d] = {" % max(len(clips), 1)]
    lines += ["    {%d, %d, %d, %d}," % (x0, y0, x1 - x0, y1 - y0) for x0, y0, x1, y1 in clips]
    lines += ["};", "", "// 0x%08x scr_table" % SCR_TABLE,
              "const IgdtScreen kScreens[%d] = {" % SCR_COUNT]
    lines += screen_rows
    layouts = load_text_layouts(elf)
    lines += ["};", "", "// 0x%08x text_layout: wchar offset (+0), max wchars (+6)" % TEXT_LAYOUT,
              "const IgdtTextLayout kTextLayouts[%d] = {" % len(layouts)]
    lines += ["    {%du, %d}," % layout for layout in layouts]
    lines += [
        "};",
        "",
        "} // namespace",
        "",
        "const IgdtSkinTables g_igdtTablesVw = {",
        "    kScreens, %d, kElements, kTextures, %d, kClips, %d," % (
            SCR_COUNT, len(textures), len(clips)),
        "    kTextLayouts, %d, %du" % (len(layouts), runtime_text_len(elf)),
        "};",
        "",
    ]
    return "\n".join(lines)


def open_stock_hmi(path):
    if not os.path.isfile(path):
        raise GenError("missing stock binary %s; extract the 5624 firmware into "
                       "extracted-fw/5624_vw (tools/build_rootfs.sh <5624 firmware .tar> "
                       "stages the same rootfs)" % path)
    with open(path, "rb") as f:
        actual = hashlib.sha256(f.read()).hexdigest()
    if actual != STOCK_SHA256:
        raise GenError("%s has sha256 %s, expected %s (firmware 5624 opt/hmi); re-extract "
                       "the 5624 firmware (see tools/build_rootfs.sh)" % (path, actual, STOCK_SHA256))
    return hmi_elf.Elf(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--hmi", default=str(DEFAULT_HMI), help="stock 5624 opt/hmi")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="generated .cpp path")
    args = parser.parse_args(argv)
    try:
        source = emit_cpp(open_stock_hmi(args.hmi))
    except (GenError, hmi_elf.ElfError) as exc:
        print("Error: %s" % exc, file=sys.stderr)
        return 1
    if not hmi_elf.write_if_changed(args.out, source):
        print("Unchanged %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
