// File name is ours; stock name unknown.
// Host tests for the IGDT dynamic runtime (display/igdt/igdt_runtime.h)
// against the generated 5624 scene tables.
#include "display/igdt/igdt_runtime.h"
#include "tests/test_runner.h"

#ifdef HMI_HAVE_IGDT_TABLES
static void test_init_and_mutations() {
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);

    // Branch 0x01000045 (VBtnInfo glow on screen 0x04) starts at child 1 in stock .rodata.
    CHECK_EQ(rt.branch[0x45], 1);
    CHECK_EQ(IgdtSetBranch(rt, 0x01000045u, 0), IGDT_OK);
    CHECK_EQ(rt.branch[0x45], 0);
    CHECK(IgdtConsumeDirty(rt));
    CHECK(!IgdtConsumeDirty(rt));

    // State 0x030000be (single App-Connect logo slot) updates cleanly.
    CHECK_EQ(IgdtSetState(rt, 0x030000beu, 2), IGDT_OK);
    CHECK_EQ(rt.state[0xbe], 2);

    // Dynamic tint color on 0x42100284 (vAppNoConnection brand line).
    CHECK_EQ(IgdtSetColor(rt, 0x42100284u, 0xff1e5affu), IGDT_OK);
    CHECK_EQ(rt.dyn_img[0x284].tint, 0xff1e5affu);

    // Dynamic text Y position on 0x45100058 (VInfoTxt on 0x04).
    CHECK_EQ(IgdtSetPosY(rt, 0x45100058u, 0x71), IGDT_OK);
    int16_t x = 0, y = 0;
    CHECK_EQ(IgdtGetPosXY(rt, 0x45100058u, &x, &y), IGDT_OK);
    CHECK_EQ(y, 0x71);

    // Dynamic group touch area (0x40100166 = VBtnInfo).
    CHECK_EQ(IgdtSetTouchArea(rt, 0x40100166u, 0, 396, 800, 84), IGDT_OK);
    IgdtClipRect area = {0, 0, 0, 0};
    CHECK_EQ(IgdtGetTouchArea(rt, 0x40100166u, &area), IGDT_OK);
    CHECK_EQ(area.y, 396);
    CHECK_EQ(area.w, 800);
    CHECK_EQ(area.h, 84);

    // Dynamic scale (3-slice stretch).
    CHECK_EQ(IgdtSetScale(rt, 0x42100284u, 2.5f, 1.0f), IGDT_OK);
    CHECK(rt.dyn_img[0x284].matrix[0] > 2.49f && rt.dyn_img[0x284].matrix[0] < 2.51f);

    // 29-layer screen show/hide: 0x04 -> layer 1, 0xfe -> layer 5.
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0x04), IGDT_OK);
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0xfe), IGDT_OK);
    CHECK_EQ(rt.layers[1], 0x04);
    CHECK_EQ(rt.layers[5], 0xfe);
    CHECK_EQ(IgdtLayerHideScreen(rt, g_igdtTablesVw, 0x04), IGDT_OK);
    CHECK_EQ(rt.layers[1], IGDT_SCREEN_NONE);
}

static void test_out_of_range_rejected() {
    // Review Focus 4: static tags, bad kinds, out-of-bounds indices, screen >= 255.
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    (void)IgdtConsumeDirty(rt);

    // Stock codes: branch index 0x1003 (0x00322860), state index 0x1005 (0x00322900).
    CHECK_EQ(IgdtSetBranch(rt, 0x010010f0u, 0), IGDT_ERR_BRANCH_RANGE);
    CHECK_EQ(IgdtSetState(rt, 0x03001b11u, 0), IGDT_ERR_STATE_RANGE);
    CHECK_EQ(IgdtSetBranch(rt, 0x03000001u, 0), IGDT_ERR_BAD_KIND);        // not a branch tag
    CHECK_EQ(IgdtSetColor(rt, 0x02000001u, 0xff00ff00u), IGDT_ERR_BAD_TAG);  // static (no bit 30)
    CHECK_EQ(IgdtSetColor(rt, 0x42006cfdu, 0xff00ff00u), IGDT_ERR_BAD_TAG);  // dyn_img OOB
    CHECK_EQ(IgdtSetAlpha(rt, 0x400030bau, 0x80), IGDT_ERR_BAD_TAG);         // dyn_grp OOB
    CHECK_EQ(IgdtSetPosY(rt, 0x45000e2au, 10), IGDT_ERR_BAD_TAG);            // dyn_txt OOB
    CHECK_EQ(IgdtSetScale(rt, 0x02000001u, 2.0f, 1.0f), IGDT_ERR_BAD_MATRIX_TAG);
    CHECK_EQ(IgdtSetTextAlign(rt, 0x45000e2au, 1, 0, 0), IGDT_ERR_RANGE);
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0xff), IGDT_ERR_SCREEN);
    CHECK(!IgdtConsumeDirty(rt));
}

static void test_init_values_and_reset() {
    IgdtRuntime rt;
    IgdtRuntimeInit(rt, g_igdtTablesVw);
    for (int i = 0; i < IGDT_LAYER_COUNT; ++i) CHECK_EQ(rt.layers[i], IGDT_SCREEN_NONE);

    // MediaOverview progress bar group 0x4010158f clips with clip_table[3741]
    // = (128, 348, 542, 15) from its group key (0x00323c48).
    IgdtClipRect area = {0, 0, 0, 0};
    CHECK_EQ(IgdtGetTouchArea(rt, 0x4010158fu, &area), IGDT_OK);
    CHECK_EQ(area.x, 128);
    CHECK_EQ(area.y, 348);
    CHECK_EQ(area.w, 542);
    CHECK_EQ(area.h, 15);

    // Dynamic text starts at its record: VInfoTxt (400, 113), untinted.
    int16_t x = 0, y = 0;
    CHECK_EQ(IgdtGetPosXY(rt, 0x45100058u, &x, &y), IGDT_OK);
    CHECK_EQ(x, 400);
    CHECK_EQ(y, 113);
    CHECK_EQ(rt.dyn_img[0x284].tint, 0xffffffffu);

    // Setting the same value is not a change.
    CHECK_EQ(IgdtSetPosXY(rt, 0x45100058u, 400, 113), IGDT_OK);
    CHECK(!IgdtConsumeDirty(rt));

    // Reset restores the record values.
    CHECK_EQ(IgdtSetPosXY(rt, 0x45100058u, 1, 2), IGDT_OK);
    CHECK_EQ(IgdtSetAlpha(rt, 0x45100058u, 0x40), IGDT_OK);
    CHECK_EQ(IgdtSetRotate(rt, 0x42100284u, 0.0f), IGDT_OK);
    CHECK(IgdtConsumeDirty(rt));
    CHECK_EQ(IgdtResetNode(rt, g_igdtTablesVw, 0x45100058u), IGDT_OK);
    CHECK(IgdtConsumeDirty(rt));
    CHECK_EQ(IgdtGetPosXY(rt, 0x45100058u, &x, &y), IGDT_OK);
    CHECK_EQ(x, 400);
    CHECK_EQ(y, 113);
    CHECK_EQ(rt.dyn_txt[0x58].alpha, 0xff);

    // Text align: three 2-bit fields of the RAM byte.
    CHECK_EQ(IgdtSetTextAlign(rt, 0x45100058u, 2, 1, 3), IGDT_OK);
    CHECK_EQ(rt.dyn_txt[0x58].align_override, 2 | 1 << 2 | 3 << 4);
    CHECK(rt.dyn_txt[0x58].has_align_override);

    // Hiding a screen that is not shown leaves its layer alone.
    CHECK_EQ(IgdtLayerShowScreen(rt, g_igdtTablesVw, 0xfe), IGDT_OK);
    CHECK_EQ(IgdtLayerHideScreen(rt, g_igdtTablesVw, 0x04), IGDT_OK);
    CHECK_EQ(rt.layers[5], 0xfe);
}
#endif

int main() {
#ifdef HMI_HAVE_IGDT_TABLES
    RUN_TEST(test_init_and_mutations);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_init_values_and_reset);
#endif
    return test_summary();
}
