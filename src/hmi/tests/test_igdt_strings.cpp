// File name is ours; stock name unknown.
// Review Focus 4: a text id with no entry for the active language falls back
// to en_US, never garbage or a crash.
#include "display/igdt/igdt_strings.h"
#include "tests/test_runner.h"

namespace {

const IgdtStringEntry kStrings[] = {
    {0x0009u, {"%1", "", "", "", ""}},                                  // language-neutral
    {0x030fu, {"Driver window", "zh-text", "pt-text", "ru-text", "es-text"}},
    {0x0360u, {"Off", "", "Off", "ru-off", ""}},                        // zh_CN, es_MX empty
    {0x0400u, {"", "", "", "", ""}},                                    // nothing at all
};

const IgdtEngString kEng[] = {
    {0x0000u, "Audio Framework Service"},
    {0x0018u, "Audio Management"},
};

const IgdtStringTables kTables = {kStrings, 4, kEng, 2};

} // namespace

// Stand-in for the generated table; only IgdtGetString/IgdtGetEngString use it.
const IgdtStringTables g_igdtStringsVw = kTables;

int main() {
    CHECK_STREQ(IgdtLookupString(kTables, 0x030f, IGDT_LANG_EN_US), "Driver window");
    CHECK_STREQ(IgdtLookupString(kTables, 0x030f, IGDT_LANG_ZH_CN), "zh-text");
    CHECK_STREQ(IgdtLookupString(kTables, 0x030f, IGDT_LANG_ES_MX), "es-text");

    // Empty zh_CN / es_MX translation -> en_US text.
    CHECK_STREQ(IgdtLookupString(kTables, 0x0360, IGDT_LANG_ZH_CN), "Off");
    CHECK_STREQ(IgdtLookupString(kTables, 0x0360, IGDT_LANG_ES_MX), "Off");
    CHECK_STREQ(IgdtLookupString(kTables, 0x0360, IGDT_LANG_RU_RU), "ru-off");

    // Neutral string: slot 0 only, same text in every language.
    CHECK_STREQ(IgdtLookupString(kTables, 0x0009, IGDT_LANG_PT_BR), "%1");

    // Unknown id (including ids 0..8 and ids past the block) -> "".
    CHECK_STREQ(IgdtLookupString(kTables, 0x0000, IGDT_LANG_EN_US), "");
    CHECK_STREQ(IgdtLookupString(kTables, 0x0310, IGDT_LANG_EN_US), "");
    CHECK_STREQ(IgdtLookupString(kTables, 0xffffffffu, IGDT_LANG_EN_US), "");

    // No en_US text either -> "".
    CHECK_STREQ(IgdtLookupString(kTables, 0x0400, IGDT_LANG_ZH_CN), "");

    // Out-of-range language behaves like missing translation.
    CHECK_STREQ(IgdtLookupString(kTables, 0x030f, -1), "Driver window");
    CHECK_STREQ(IgdtLookupString(kTables, 0x030f, 99), "Driver window");

    // Public wrappers use g_igdtStringsVw.
    CHECK_STREQ(IgdtGetString(0x0360, IGDT_LANG_ZH_CN), "Off");
    CHECK_STREQ(IgdtGetString(0x9999, IGDT_LANG_EN_US), "");

    CHECK_STREQ(IgdtGetEngString(0x0018), "Audio Management");
    CHECK_STREQ(IgdtGetEngString(0x0019), "");

    return TEST_RESULT();
}
