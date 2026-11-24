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

// One drawable scene node with its initial record values.
struct IgdtElement {
    uint8_t type;       // IGDT_ELEM_IMG, IGDT_ELEM_TXT or IGDT_ELEM_RECT
    uint32_t tag;       // node tag (kind, dynamic bit 0x40000000, record index)
    int16_t x, y, w, h;
    uint8_t alpha;
    uint32_t color;     // tint for img, text color for txt, fill color for rect
    int32_t tex;        // tex_table index for img, -1 otherwise (5606 has
                        // 39,367 slots, so int16_t would not do)
    uint8_t font;       // text_layout font (txt only)
    uint8_t align;      // text_layout align (txt only)
    uint32_t str_id;    // txt only: node kind 4 -> wchar offset for IgdtGetEngString();
                        // kind 5 -> IGDT_STR_RUNTIME (page code supplies the text)
    uint32_t rt_off;    // kind 5 txt: wchar offset into the runtime text buffer
                        // (text_layout +0, FUN_0031f41c local_58); 0 otherwise
    int16_t clip;       // IgdtSkinTables.clips index in effect, -1 = screen only
    uint32_t bg_color;  // txt only: text record +0xc background fill (0x0031dffc)
};

// Clip in effect for an element: enclosing groups' clip_table (0x0045cca0)
// rects intersected as FUN_0031da6c does. w == 0 && h == 0 draws nothing.
struct IgdtClipRect {
    int16_t x, y, w, h;
};

// 0x004536a4 scr_table entry (16 B); elements are kElements[elem_first ..
// elem_first + elem_count) in scene-walk draw order.
struct IgdtScreen {
    uint16_t id;
    const char* name;
    uint8_t layer_group;
    uint16_t elem_first, elem_count;
};

// 0x0046d6a0 text_layout entry (20 B), the part the runtime text store
// (0x003223d4) uses; indexed by widget text id < IgdtSkinTables.text_layout_count.
struct IgdtTextLayout {
    uint32_t off;       // +0 wchar offset into the runtime text buffer
    uint8_t max_chars;  // +6 wchars copied (wcsncpy limit)
};

struct IgdtSkinTables {
    const IgdtScreen* screens;
    uint16_t screen_count;
    const IgdtElement* elements;
    const IgdtTexEntry* textures;
    uint16_t tex_count;
    const IgdtClipRect* clips;
    uint16_t clip_count;
    const IgdtTextLayout* text_layouts;
    uint16_t text_layout_count;
    uint32_t text_buffer_len;   // wchars of the runtime text buffer (0x00c62264)
};

extern const IgdtSkinTables g_igdtTablesVw;

#endif
