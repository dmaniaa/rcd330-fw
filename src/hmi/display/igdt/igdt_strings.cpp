// File name is ours; stock name unknown.
// Lookup and language fallback over the generated string tables
// (build/hmi/gen/strings_vw.cpp); the data itself is decoded at build time.
#include "display/igdt/igdt_strings.h"

namespace {

template <typename T>
const T* findById(const T* rows, unsigned count, uint32_t id) {
    unsigned lo = 0, hi = count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (rows[mid].id < id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return (lo < count && rows[lo].id == id) ? &rows[lo] : 0;
}

bool hasText(const char* s) {
    return s != 0 && s[0] != '\0';
}

} // namespace

// 0x00023650 IgdtGetString
// DEVIATION: stock decodes the compressed block on demand and draws an empty
// string for a language slot without a translation; we decode at build time
// and fall back to the en_US text (and "" for unknown ids) instead.
const char* IgdtLookupString(const IgdtStringTables& tables, uint32_t str_id, int lang) {
    const IgdtStringEntry* row = findById(tables.strings, tables.string_count, str_id);
    if (row == 0) {
        return "";
    }
    if (lang >= 0 && lang < IGDT_LANG_COUNT && hasText(row->text[lang])) {
        return row->text[lang];
    }
    return hasText(row->text[IGDT_LANG_EN_US]) ? row->text[IGDT_LANG_EN_US] : "";
}

const char* IgdtGetString(uint32_t str_id, int lang) {
    return IgdtLookupString(g_igdtStringsVw, str_id, lang);
}

// Helper derived from 0x0031f41c (text draw, kind 4 branch); not a stock function.
// Stock reads the wide text straight from the UTF-32 table at 0x008a4c00 + 4 * offset.
const char* IgdtLookupEngString(const IgdtStringTables& tables, uint32_t eng_id) {
    const IgdtEngString* row = findById(tables.eng_strings, tables.eng_count, eng_id);
    return (row != 0 && row->text != 0) ? row->text : "";
}

const char* IgdtGetEngString(uint32_t eng_id) {
    return IgdtLookupEngString(g_igdtStringsVw, eng_id);
}
