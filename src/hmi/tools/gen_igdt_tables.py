#!/usr/bin/env python3
"""Generate the IGDT scene/texture C++ tables from stock 5624 opt/hmi .rodata.

Reads scr_table, the 12-byte scene node graph, the static/dynamic image and
text records, text_layout and tex_table (docs/hmi-internals.md section 6,
docs/assets-and-localization.md section 1) and writes
build/hmi/gen/igdt_tables_vw.cpp for src/hmi/display/igdt/igdt_tables.h.

The C++ output is the live scene graph (load_scene_graph): every node with
its contiguous child slice, the static and dynamic record tables, the
branch/state init tables, group flags, clip_table and per-screen touch paths,
walked at runtime by src/hmi/display/igdt/igdt_scene.cpp.

SceneWalker / load_screens() flatten each screen in scene-walk draw order with
every branch/state node resolved to its initial child, exactly as walked by
FUN_0032072c -> FUN_0031f8e4 / FUN_003202c4 / FUN_0031f0a0 / FUN_0031f41c;
they check the graph against resources/scenes/scenes_5624_vw.json, which came
from an earlier extractor with known defects (tools/tests/test_hmi_tables.py
lists where this output differs).
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
TEX_COUNT = (TEX_TABLE_END - TEX_TABLE) // TEX_STRIDE
NODE_STRIDE = 12            # <u16 eidx, u16 ccnt, u32 tag, u32 cptr>
BRANCH_INIT, BRANCH_COUNT = 0x78f0c4, 0x10f0   # u16 each, memcpy'd to RAM 0xc502d8 by FUN_00324328
STATE_INIT, STATE_COUNT = 0x7912a4, 0x1b11     # u16 each, memcpy'd to RAM 0xc524b8 by FUN_00324328
# Dynamic record counts: the RAM loops of FUN_00324328.
DYN_GRP_COUNT, DYN_IMG_COUNT, DYN_TXT_COUNT = 0x30ba, 0x6cfd, 0xe2a
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
    # Mislabel kept from scenes_5624_vw.json so the oracle test still
    # matches: 0xfe is VSoundVolumeSetup (ctor 0x001b958c), not a status bar;
    # the status line is part of 0xf9 / 0x04 (docs/hmi-internals.md section 7).
    0xfe: "StatusBar",
}

ELEM_IMG, ELEM_TXT, ELEM_RECT = 2, 4, 7   # IgdtElement.type


class GenError(Exception):
    pass


def screen_name(sid):
    return SCREEN_NAMES.get(sid, "Screen_0x%02x" % sid)


def load_textures(elf):
    """All tex_table entries as <BBHhhIHHII> tuples, in table order."""
    return [elf.unpack("<BBHhhIHHII", TEX_TABLE + i * TEX_STRIDE) for i in range(TEX_COUNT)]


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


def load_full_text_layouts(elf, count):
    """(off, max_chars, width, font, align, flags) of text_layout entries
    0..count-1: +0 u32 wchar offset, +6 u8 max wchars, +4 u16 box width,
    +7 u8 font, +8 u8 align (FUN_0031dfac fields), +0x10 u8 flags (bit 1:
    runtime buffer)."""
    rows = []
    for i in range(count):
        va = TEXT_LAYOUT + i * TEXT_LAYOUT_STRIDE
        off, width, max_chars, font, align = elf.unpack("<IHBBB", va)
        rows.append((off, max_chars, width, font, align, elf.u8(va + 0x10)))
    return rows


def runtime_text_len(elf):
    """wchars of the runtime text buffer (0x00c62264): the furthest end of a
    layout whose +0x10 bit 1 selects that buffer (FUN_0031ee54)."""
    end = 0
    for i, (offset, max_chars) in enumerate(load_text_layouts(elf)):
        if elf.u8(TEXT_LAYOUT + i * TEXT_LAYOUT_STRIDE + 0x10) & 2:
            end = max(end, offset + max_chars)
    return end


# Record layouts (little-endian), checked against FUN_003202c4 (group),
# FUN_0031f0a0 (image), FUN_0031f41c (text), FUN_0031f8e4 kind 7 (rect) and
# the RAM initializers FUN_00323c48 / FUN_00323dfc / FUN_00323f70.
GRP_FMT = "<BxHhh9f2f"      # 0x34: alpha, group key, x, y, matrix +8, pivot +0x2c
IMG_FMT = "<BxxxhhI9f2f"   # 0x38: alpha, x, y, colour +8, matrix +0xc, pivot +0x30
TXT_FMT = "<BxHhhII9f2f"    # 0x3c: alpha, layout, x, y, fg +8, bg +0xc, matrix +0x10, pivot +0x34
RECT_FMT = "<Bxxxhh9f2f"    # 0x34: alpha, x, y, matrix +8, pivot +0x2c
IDENTITY = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
TOUCH_PATH_MAX = 32         # bytes per touch path (root index + steps), ours


def touch_path(elf, path_va, nodes, index, root_va, root_count, sid):
    """Bytes of one touch path as FUN_00324b90 follows them: a root index,
    then child steps until the first step >= the node's child count (that
    sentinel byte included)."""
    steps = [elf.u8(path_va)]
    if steps[0] >= root_count:
        raise GenError("screen 0x%02x touch path %#x: root index %d >= %d roots" % (
            sid, path_va, steps[0], root_count))
    node = index[root_va + steps[0] * NODE_STRIDE]
    while True:
        if len(steps) == TOUCH_PATH_MAX:
            raise GenError("screen 0x%02x touch path %#x longer than %d bytes" % (
                sid, path_va, TOUCH_PATH_MAX))
        step = elf.u8(path_va + len(steps))
        steps.append(step)
        _, ccnt, _, first = nodes[node]
        if step >= ccnt:
            return steps
        node = first + step


def load_scene_graph(elf):
    """The live scene graph and init tables the runtime walks (igdt_tables.h).

    nodes: every node reachable from the 255 screens' root lists, in stock
    .rodata order, as (eidx, child_count, tag, child_first or -1); stock's
    child slices cptr[0..ccnt) stay contiguous. touch_paths index touch_steps.
    """
    screens_raw = [elf.unpack("<BBBBIII", SCR_TABLE + sid * 16) for sid in range(SCR_COUNT)]
    slices = {}     # child slice VA -> largest child count seen
    for _, _, root_count, _, _, _, root in screens_raw:
        if root and root_count:
            slices[root] = max(slices.get(root, 0), root_count)
    walked = {}
    pending = list(slices.items())
    while pending:
        va, count = pending.pop()
        if walked.get(va, 0) >= count:
            continue
        walked[va] = count
        for i in range(count):
            _, ccnt, _, cptr = elf.unpack("<HHII", va + i * NODE_STRIDE)
            if cptr and ccnt:
                pending.append((cptr, ccnt))
    node_vas = sorted({va + i * NODE_STRIDE for va, count in walked.items() for i in range(count)})
    index = {va: i for i, va in enumerate(node_vas)}
    for va, count in walked.items():
        if any(index[va + i * NODE_STRIDE] != index[va] + i for i in range(count)):
            raise GenError("child slice at %#x is not contiguous in the node table" % va)
    nodes = []
    for va in node_vas:
        eidx, ccnt, tag, cptr = elf.unpack("<HHII", va)
        nodes.append((eidx, ccnt, tag, index[cptr] if cptr and ccnt else -1))

    screens, touch_paths, touch_steps = {}, [], []
    for sid, (layer_group, _, root_count, touch_count, touch_ptr, bg_color, root) in enumerate(screens_raw):
        touch_first = len(touch_paths)
        for k in range(touch_count if touch_ptr else 0):
            steps = touch_path(elf, elf.u32(touch_ptr + k * 4), nodes, index, root, root_count, sid)
            touch_paths.append((len(touch_steps), len(steps)))
            touch_steps.extend(steps)
        screens[sid] = {
            "layer_group": layer_group,
            "root_first": index[root] if root and root_count else 0,
            "root_count": root_count if root else 0,
            "touch_first": touch_first,
            "touch_count": len(touch_paths) - touch_first,
            "bg_color": bg_color,
        }

    def count_of(kind, dynamic):
        used = [tag & TAG_INDEX_MASK for _, _, tag, _ in nodes
                if (tag >> 24) & 0xf == kind and bool(tag & TAG_DYNAMIC) == dynamic]
        return max(used) + 1 if used else 0

    def records(base, stride, fmt, count):
        return [elf.unpack(fmt, base + i * stride) for i in range(count)]

    static_txt_count = max(count_of(KIND_TXT, False), count_of(KIND_TXT_DYN, False))
    graph = {
        "nodes": nodes,
        "node_base": node_vas[0],
        "screens": screens,
        "touch_paths": touch_paths,
        "touch_steps": touch_steps,
        "static_grps": records(STATIC_GRP, GRP_STRIDE, GRP_FMT, count_of(KIND_GROUP, False)),
        "dyn_grps": records(DYN_GRP, GRP_STRIDE, GRP_FMT, DYN_GRP_COUNT),
        "static_imgs": records(STATIC_IMG, IMG_STRIDE, IMG_FMT, count_of(KIND_IMG, False)),
        "dyn_imgs": records(DYN_IMG, IMG_STRIDE, IMG_FMT, DYN_IMG_COUNT),
        "static_txts": records(STATIC_TXT, TXT_STRIDE, TXT_FMT, static_txt_count),
        "dyn_txts": records(DYN_TXT, TXT_STRIDE, TXT_FMT, DYN_TXT_COUNT),
        "branch_init": [elf.u16(BRANCH_INIT + i * 2) for i in range(BRANCH_COUNT)],
        "state_init": [elf.u16(STATE_INIT + i * 2) for i in range(STATE_COUNT)],
    }
    # Text nodes index text_layout by eidx (FUN_003220dc) beyond the 0xe3f ids
    # the runtime text store accepts (engineering text up to 0xe8a in 5624).
    used = [eidx for eidx, _, tag, _ in nodes if (tag >> 24) & 0xf in (KIND_TXT, KIND_TXT_DYN)]
    used += [rec[1] for rec in graph["dyn_txts"]]
    graph["text_layouts"] = load_full_text_layouts(elf, max([TEXT_LAYOUT_COUNT - 1] + used) + 1)
    check_scene_graph(graph)

    # Kind 7 (static only): record by tag index, w/h/colour by node eidx.
    rect_nodes = [(eidx, tag & TAG_INDEX_MASK) for eidx, _, tag, _ in nodes
                  if (tag >> 24) & 0xf == KIND_RECT and not tag & TAG_DYNAMIC]
    rect_count = max(max(e, i) for e, i in rect_nodes) + 1 if rect_nodes else 0
    rects = []
    for i in range(rect_count):
        alpha, x, y = elf.unpack(RECT_FMT, STATIC_RECT + i * RECT_STRIDE)[:3]
        w, h, color = elf.unpack("<HHI", RECT_LAYOUT + i * RECT_LAYOUT_STRIDE)
        rects.append((alpha, x, y, w, h, color))
    for _, i in rect_nodes:
        rec = elf.unpack(RECT_FMT, STATIC_RECT + i * RECT_STRIDE)
        if rec[3:12] != IDENTITY or rec[12:] != (0.0, 0.0):
            raise GenError("rect record %d has a matrix/pivot; IgdtRectRecord has none" % i)
    graph["rects"] = rects

    # Group flags by group eidx (FUN_003202c4) and dyn group key (FUN_00323c48).
    keys = [eidx for eidx, _, tag, _ in nodes if (tag >> 24) & 0xf == KIND_GROUP]
    keys += [rec[1] for rec in graph["dyn_grps"]]
    graph["grp_flags"] = [elf.unpack("<BBH", GROUP_FLAGS + i * 4) for i in range(max(keys) + 1)]
    clip_count = max(clip for _, _, clip in graph["grp_flags"]) + 1
    graph["clips"] = [elf.unpack("<hhhh", CLIP_TABLE + i * 8) for i in range(clip_count)]
    for eidx, ccnt, tag, _ in nodes:
        if (tag >> 24) & 0xf == KIND_GROUP and ccnt:
            flags = graph["grp_flags"][eidx][0]
            if flags & GROUP_PLAIN != GROUP_PLAIN:
                raise GenError("flow-layout group tag %#010x (flags %#04x) has children: "
                               "unsupported by igdt_scene.cpp" % (tag, flags))
    return graph


def check_scene_graph(graph):
    """Indices the C++ walker uses without further checks."""
    layouts = len(graph["text_layouts"])
    for eidx, _, tag, _ in graph["nodes"]:
        kind = (tag >> 24) & 0xf
        if kind == KIND_IMG and eidx >= TEX_COUNT:
            raise GenError("img node %#010x: tex index %d outside tex_table" % (tag, eidx))
        if kind in (KIND_TXT, KIND_TXT_DYN) and eidx >= layouts:
            raise GenError("txt node %#010x: layout %d >= %d" % (tag, eidx, layouts))
        if tag & TAG_DYNAMIC:
            limit = {KIND_GROUP: DYN_GRP_COUNT, KIND_IMG: DYN_IMG_COUNT,
                     KIND_TXT: DYN_TXT_COUNT, KIND_TXT_DYN: DYN_TXT_COUNT}.get(kind)
            if limit is not None and (tag & TAG_INDEX_MASK) >= limit:
                raise GenError("node %#010x: dynamic record index >= %#x" % (tag, limit))
    for i, rec in enumerate(graph["dyn_txts"]):
        if rec[1] >= layouts:
            raise GenError("dyn txt record %d: layout %d >= %d" % (i, rec[1], layouts))


def c_float(v):
    if v != v or v in (float("inf"), float("-inf")):
        raise GenError("non-finite float %r in a scene record" % v)
    s = "%.9g" % v
    if not any(c in s for c in ".e"):
        s += ".0"
    return s + "f"


def c_floats(values):
    if tuple(values) == IDENTITY:
        return "kId"   # every 5624 record matrix; keeps the generated file small
    return "{" + ", ".join(c_float(v) for v in values) + "}"


def emit_grp(rec):
    alpha, key, x, y = rec[:4]
    return "    {%d, %d, %d, %d, %s, %s, %s}," % (alpha, key, x, y, c_floats(rec[4:13]),
                                               c_float(rec[13]), c_float(rec[14]))


def emit_img(rec):
    alpha, x, y, tint = rec[:4]
    return "    {%d, %d, %d, 0x%08xu, %s, %s, %s}," % (alpha, x, y, tint, c_floats(rec[4:13]),
                                                     c_float(rec[13]), c_float(rec[14]))


def emit_txt(rec):
    alpha, layout, x, y, fg, bg = rec[:6]
    return "    {%d, %d, %d, %d, 0x%08xu, 0x%08xu, %s, %s, %s}," % (
        alpha, layout, x, y, fg, bg, c_floats(rec[6:15]), c_float(rec[15]), c_float(rec[16]))


def emit_cpp(elf):
    textures = load_textures(elf)
    if len(textures) > 0xffff:
        raise GenError("tex_table has %d entries; IgdtSkinTables.tex_count is uint16_t" % len(textures))
    graph = load_scene_graph(elf)
    lines = [
        "// File name is ours; stock name unknown.",
        "// Generated by src/hmi/tools/gen_igdt_tables.py from stock opt/hmi",
        "// (firmware 5624, sha256 %s). Do not edit." % STOCK_SHA256,
        '#include "display/igdt/igdt_tables.h"',
        "",
        "namespace {",
        "",
        "#define kId {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}",
        "",
        "// 0x%08x tex_table" % TEX_TABLE,
        "const IgdtTexEntry kTextures[%d] = {" % len(textures),
    ]
    for fmt, flags, a0, a1, a2, bsz, w, h, tint, path_va in textures:
        lines.append("    {0x%02x, 0x%02x, 0x%04x, %d, %d, %du, %d, %d, 0x%08xu, %s}," % (
            fmt, flags, a0, a1, a2, bsz, w, h, tint, hmi_elf.c_string(elf.cstr(path_va))))

    lines += ["};", "", "// 0x%08x clip_table: x, y, w, h relative to the current origin" % CLIP_TABLE,
              "const IgdtClipRect kClips[%d] = {" % len(graph["clips"])]
    lines += ["    {%d, %d, %d, %d}," % c for c in graph["clips"]]

    lines += ["};", "", "// 0x%08x scr_table" % SCR_TABLE, "const IgdtScreen kScreens[%d] = {" % SCR_COUNT]
    for sid in range(SCR_COUNT):
        s = graph["screens"][sid]
        lines.append("    {0x%02x, %s, %d, %du, %d, %du, %d, 0x%08xu}," % (
            sid, hmi_elf.c_string(screen_name(sid)), s["layer_group"], s["root_first"], s["root_count"],
            s["touch_first"], s["touch_count"], s["bg_color"]))

    layouts = graph["text_layouts"]
    lines += ["};", "", "// 0x%08x text_layout: offset, max wchars, width, font, align, flags" % TEXT_LAYOUT,
              "const IgdtTextLayout kTextLayouts[%d] = {" % len(layouts)]
    lines += ["    {%du, %d, %d, %d, 0x%02x, 0x%02x}," % layout for layout in layouts]

    nodes = graph["nodes"]
    lines += ["};", "", "// Scene nodes 0x%08x.. (12 B each): eidx, child count, tag, first child" % (
        graph["node_base"]), "const IgdtNode kNodes[%d] = {" % len(nodes)]
    lines += ["    {%d, %d, 0x%08xu, %d}," % n for n in nodes]

    tables = [
        ("kStaticGrps", "IgdtGrpRecord", STATIC_GRP, graph["static_grps"], emit_grp),
        ("kDynGrps", "IgdtGrpRecord", DYN_GRP, graph["dyn_grps"], emit_grp),
        ("kStaticImgs", "IgdtImgRecord", STATIC_IMG, graph["static_imgs"], emit_img),
        ("kDynImgs", "IgdtImgRecord", DYN_IMG, graph["dyn_imgs"], emit_img),
        ("kStaticTxts", "IgdtTxtRecord", STATIC_TXT, graph["static_txts"], emit_txt),
        ("kDynTxts", "IgdtTxtRecord", DYN_TXT, graph["dyn_txts"], emit_txt),
    ]
    for name, ctype, va, rows, emit in tables:
        lines += ["};", "", "// 0x%08x" % va, "const %s %s[%d] = {" % (ctype, name, max(len(rows), 1))]
        lines += [emit(r) for r in rows]

    lines += ["};", "", "// 0x%08x rect records (alpha, x, y) + 0x%08x rect layouts (w, h, colour)" % (
        STATIC_RECT, RECT_LAYOUT), "const IgdtRectRecord kRects[%d] = {" % max(len(graph["rects"]), 1)]
    lines += ["    {%d, %d, %d, %d, %d, 0x%08xu}," % r for r in graph["rects"]]
    lines += ["};", "", "// 0x%08x group flags: flags, flow spacing, clip_table index" % GROUP_FLAGS,
              "const IgdtGrpFlags kGrpFlags[%d] = {" % len(graph["grp_flags"])]
    lines += ["    {0x%02x, %d, %d}," % f for f in graph["grp_flags"]]
    for name, va, values in (("kBranchInit", BRANCH_INIT, graph["branch_init"]),
                             ("kStateInit", STATE_INIT, graph["state_init"])):
        lines += ["};", "", "// 0x%08x" % va, "const uint16_t %s[%d] = {" % (name, len(values))]
        lines += ["    " + ", ".join(str(v) for v in values[i:i + 16]) + ","
                  for i in range(0, len(values), 16)]
    paths, steps = graph["touch_paths"], graph["touch_steps"]
    lines += ["};", "", "// Touch paths of every screen (scr_table +3 count, +4 pointer list)",
              "const IgdtTouchPath kTouchPaths[%d] = {" % len(paths)]
    lines += ["    {%du, %d}," % p for p in paths]
    lines += ["};", "", "const uint8_t kTouchSteps[%d] = {" % len(steps)]
    lines += ["    " + ", ".join(str(v) for v in steps[i:i + 24]) + "," for i in range(0, len(steps), 24)]
    lines += [
        "};",
        "",
        "#undef kId",
        "",
        "} // namespace",
        "",
        "const IgdtSkinTables g_igdtTablesVw = {",
        "    kScreens, %d, kTextures, %d, kClips, %d," % (SCR_COUNT, len(textures), len(graph["clips"])),
        "    kTextLayouts, %d, %du," % (TEXT_LAYOUT_COUNT, runtime_text_len(elf)),
        "    kNodes, %du," % len(nodes),
        "    kStaticGrps, %d, kDynGrps," % len(graph["static_grps"]),
        "    kStaticImgs, %d, kDynImgs," % len(graph["static_imgs"]),
        "    kStaticTxts, %d, kDynTxts," % len(graph["static_txts"]),
        "    kRects, %d, kGrpFlags, %d," % (len(graph["rects"]), len(graph["grp_flags"])),
        "    kBranchInit, kStateInit,",
        "    kTouchPaths, %d, kTouchSteps, %du" % (len(paths), len(steps)),
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
