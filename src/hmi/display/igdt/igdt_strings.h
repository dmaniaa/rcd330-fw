// File name is ours; stock name unknown.
//
// Localized UI strings: the stock compressed 5-language block (FUN_00023650,
// docs/assets-and-localization.md section 2) decoded at build time by
// src/hmi/tools/gen_strings.py into build/hmi/gen/strings_vw.cpp, plus the
// UTF-32 engineering string table. All text is UTF-8.
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_STRINGS_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_STRINGS_H

#include <stdint.h>

// Stock language slot order; the 6-language table is phase 4.
enum {
    IGDT_LANG_EN_US = 0,
    IGDT_LANG_ZH_CN = 1,
    IGDT_LANG_PT_BR = 2,
    IGDT_LANG_RU_RU = 3,
    IGDT_LANG_ES_MX = 4,
    IGDT_LANG_COUNT = 5
};

// One string group; id is its byte offset in the stock block. Language-neutral
// strings (id < 0x2d2) carry their text in slot 0 only.
struct IgdtStringEntry {
    uint32_t id;
    const char* text[IGDT_LANG_COUNT];
};

// One engineering string; id is its wchar offset in the UTF-32 table.
struct IgdtEngString {
    uint32_t id;
    const char* text;
};

// Both arrays are sorted by ascending id.
struct IgdtStringTables {
    const IgdtStringEntry* strings;
    unsigned string_count;
    const IgdtEngString* eng_strings;
    unsigned eng_count;
};

extern const IgdtStringTables g_igdtStringsVw;

// Text of string str_id in language lang: an empty or missing translation
// falls back to en_US, an unknown id or no text at all gives "". Never NULL.
const char* IgdtLookupString(const IgdtStringTables& tables, uint32_t str_id, int lang);
const char* IgdtGetString(uint32_t str_id, int lang);

// Engineering test-mode text at wchar offset eng_id ("" when unknown).
const char* IgdtLookupEngString(const IgdtStringTables& tables, uint32_t eng_id);
const char* IgdtGetEngString(uint32_t eng_id);

#endif
