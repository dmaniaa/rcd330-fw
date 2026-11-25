// File name is ours; stock name unknown.
//
// IGDT skin tables: stock scr_table / scene graph / tex_table (firmware 5624
// .rodata, docs/hmi-internals.md section 6), generated at build time into
// build/hmi/gen/igdt_tables_vw.cpp by src/hmi/tools/gen_igdt_tables.py.
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_TABLES_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_TABLES_H

#include <stdint.h>

// 0x0069b69c tex_table entry (24 B, <BBHhhIHHII>), docs/assets-and-localization.md section 1.
struct IgdtTexEntry {
    uint8_t fmt, flags;
    uint16_t a0;
    int16_t a1, a2;
    uint32_t bsz;
    uint16_t w, h;
    uint32_t tint;
    const char* path;   // "gresfiles/..._ovg.bin", relative to /opt
};

enum {
    IGDT_ELEM_IMG = 2,  // scene node kind 0x2 (static_img / dyn_img)
    IGDT_ELEM_TXT = 4,  // scene node kinds 0x4 / 0x5 (static_txt / dyn_txt)
    IGDT_ELEM_RECT = 7  // scene node kind 0x7, static only (0x896cd0 record, 0x8a4be8 w/h/color)
};

const uint32_t IGDT_STR_RUNTIME = 0xffffffffu;

// One drawable node resolved by the scene walk (igdt_scene.cpp) for the
// device layer: IgdtDrawImage reads type/tex/w/h, IgdtDrawText font/align/w.
struct IgdtElement {
    uint8_t type;       // IGDT_ELEM_IMG, IGDT_ELEM_TXT or IGDT_ELEM_RECT
    uint32_t tag;       // node tag (kind, dynamic bit 0x40000000, record index)
    int16_t x, y, w, h; // record position; img: texture size; txt: layout width / max wchars
    uint8_t alpha;
    uint32_t color;     // img record +8, text colour for txt, fill colour for rect
    int32_t tex;        // tex_table index for img, -1 otherwise (5606 has
                        // 39,367 slots, so int16_t would not do)
    uint8_t font;       // text_layout font (txt only)
    uint8_t align;      // text_layout align (txt only)
    uint32_t str_id;    // txt only: node kind 4 -> wchar offset for IgdtGetEngString();
                        // kind 5 -> IGDT_STR_RUNTIME (page code supplies the text)
    uint32_t rt_off;    // kind 5 txt: wchar offset into the runtime text buffer
                        // (text_layout +0, FUN_0031f41c local_58); 0 otherwise
    uint32_t bg_color;  // txt only: text record +0xc background fill (0x0031dffc)
};

// x, y, w, h. clip_table (0x0045cca0) entries and dynamic group touch areas
// are relative to the current origin (0x0031d9cc).
struct IgdtClipRect {
    int16_t x, y, w, h;
};

// Node tag fields: bits 24..27 kind, bit 30 dynamic record, bits 20..23
// sub-kind (0x1 / 0x3 / 0x4 = record carries a RAM matrix), bits 0..19
// record index.
enum {
    IGDT_KIND_GROUP = 0x0,
    IGDT_KIND_BRANCH = 0x1,
    IGDT_KIND_IMG = 0x2,
    IGDT_KIND_STATE = 0x3,
    IGDT_KIND_TXT = 0x4,
    IGDT_KIND_TXT_DYN = 0x5,
    IGDT_KIND_RECT = 0x7,
    IGDT_KIND_IMG8 = 0x8,      // runtime-image node (0x00133734); not drawn yet
    IGDT_KIND_SCREEN = 0x9     // screen tag (scr_table +1 | 0x9000000, 0x0032072c)
};
const uint32_t IGDT_TAG_DYNAMIC = 0x40000000u;
const uint32_t IGDT_TAG_KIND_MASK = 0x0f000000u;
const uint32_t IGDT_TAG_INDEX_MASK = 0x000fffffu;

// Fixed table sizes of firmware 5624 (the RAM loops of 0x00324328 and the
// bounds of 0x00322860 / 0x00322900).
enum {
    IGDT_DYN_GRP_COUNT = 0x30ba,
    IGDT_DYN_IMG_COUNT = 0x6cfd,
    IGDT_DYN_TXT_COUNT = 0xe2a,
    IGDT_BRANCH_COUNT = 0x10f0,
    IGDT_STATE_COUNT = 0x1b11
};

// 0x007948e4.. scene node (12 B <HHII>); cptr is replaced by the index of
// the first child in IgdtSkinTables.nodes (children are contiguous, as
// stock's cptr[0 .. child_count)), -1 when there are none.
struct IgdtNode {
    uint16_t eidx;          // tex index (img), text_layout (txt), group flags (group),
                            // branch / state slot (branch / state), rect layout (rect)
    uint16_t child_count;
    uint32_t tag;
    int32_t child_first;
};

// Group record (0x34 B): static 0x00875c14, dynamic init 0x0047f97c.
struct IgdtGrpRecord {
    uint8_t alpha;          // +0x00
    uint16_t key;           // +0x02 group flags index (dynamic: clip of 0x00323c48)
    int16_t x, y;           // +0x04 (not used by the walk: dynamic groups start at 0, 0)
    float matrix[9];        // +0x08 column-major 3x3
    float pivot_x, pivot_y; // +0x2c
};

// Image record (0x38 B): static 0x00896d38, dynamic init 0x0051df44.
struct IgdtImgRecord {
    uint8_t alpha;          // +0x00
    int16_t x, y;           // +0x04
    uint32_t tint;          // +0x08 -> draw state fg colour (0x0031dfec); the
                            //        texture tint is the RAM colour (IgdtSetColor)
    float matrix[9];        // +0x0c
    float pivot_x, pivot_y; // +0x30
};

// Text record (0x3c B): static 0x00874b70, dynamic init 0x00759eec.
struct IgdtTxtRecord {
    uint8_t alpha;          // +0x00
    uint16_t layout;        // +0x02 text_layout index (font / align of 0x00323f70)
    int16_t x, y;           // +0x04
    uint32_t fg_color;      // +0x08
    uint32_t bg_color;      // +0x0c
    float matrix[9];        // +0x10
    float pivot_x, pivot_y; // +0x34
};

// Kind-7 rect: alpha/x/y of record i (0x00896cd0, 0x34 B, identity matrix
// and zero pivot in 5624, checked by the generator) and w/h/colour of rect
// layout i (0x008a4be8, 8 B). Nodes pick the record by tag index and the
// layout by eidx.
struct IgdtRectRecord {
    uint8_t alpha;
    int16_t x, y;
    uint16_t w, h;
    uint32_t color;
};

// 0x00454694 group flags (4 B per group eidx): bits 0-1 clip children,
// bits 2-3 == 3 plain child walk (else flow layout), flow spacing, clip_table index.
struct IgdtGrpFlags {
    uint8_t flags;
    uint8_t spacing;
    uint16_t clip_idx;
};

// One touch path (0x00324b90): touch_steps[step_first] is a root index of
// the screen, each following byte a child step, the last one the first step
// >= that node's child count.
struct IgdtTouchPath {
    uint32_t step_first;
    uint16_t step_count;
};

// 0x004536a4 scr_table entry (16 B): +0 layer group, +2 root count, +3
// touch path count, +4 touch path list, +8 background colour, +0xc root
// nodes (nodes[root_first .. root_first + root_count)).
struct IgdtScreen {
    uint16_t id;
    const char* name;
    uint8_t layer_group;
    uint32_t root_first;
    uint16_t root_count;
    uint32_t touch_first;   // touch_paths[touch_first .. touch_first + touch_count)
    uint16_t touch_count;
    uint32_t bg_color;
};

// 0x0046d6a0 text_layout entry (20 B).
struct IgdtTextLayout {
    uint32_t off;       // +0x00 wchar offset (runtime buffer or engineering table)
    uint8_t max_chars;  // +0x06 wchars copied (wcsncpy limit)
    uint16_t width;     // +0x04 box width
    uint8_t font;       // +0x07 font style
    uint8_t align;      // +0x08 bits 0-1 horizontal, 4-5 background, 6-7 truncation
    uint8_t flags;      // +0x10 bit 0 glow, bit 1 runtime buffer
};

struct IgdtSkinTables {
    const IgdtScreen* screens;
    uint16_t screen_count;
    const IgdtTexEntry* textures;
    uint16_t tex_count;
    const IgdtClipRect* clips;          // 0x0045cca0 clip_table
    uint16_t clip_count;
    // Every layout a text node uses; ids below text_layout_count (stock
    // 0xe3f) are the ones the runtime text store (0x003223d4) accepts.
    const IgdtTextLayout* text_layouts;
    uint16_t text_layout_count;
    uint32_t text_buffer_len;   // wchars of the runtime text buffer (0x00c62264)
    const IgdtNode* nodes;
    uint32_t node_count;
    const IgdtGrpRecord* static_grps;
    uint16_t static_grp_count;
    const IgdtGrpRecord* dyn_grps;      // IGDT_DYN_GRP_COUNT
    const IgdtImgRecord* static_imgs;
    uint16_t static_img_count;
    const IgdtImgRecord* dyn_imgs;      // IGDT_DYN_IMG_COUNT
    const IgdtTxtRecord* static_txts;
    uint16_t static_txt_count;
    const IgdtTxtRecord* dyn_txts;      // IGDT_DYN_TXT_COUNT
    const IgdtRectRecord* rects;
    uint16_t rect_count;
    const IgdtGrpFlags* grp_flags;
    uint16_t grp_flag_count;
    const uint16_t* branch_init;        // 0x0078f0c4, IGDT_BRANCH_COUNT
    const uint16_t* state_init;         // 0x007912a4, IGDT_STATE_COUNT
    const IgdtTouchPath* touch_paths;
    uint16_t touch_path_count;
    const uint8_t* touch_steps;
    uint32_t touch_step_count;
};

extern const IgdtSkinTables g_igdtTablesVw;

#endif
