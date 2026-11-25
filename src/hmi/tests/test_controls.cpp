// File name is ours; stock name unknown.
//
// Portable halves of the control library (display/base/control_layout):
// scene-node lookup (ControlCreator::GetElementRoot 0x002d6884), element
// rects (0x002d690c), CompositeItem layout (0x002c98f8 / 0x002c9168 /
// 0x002c9340) applied to the IGDT runtime, and the IgdtButtonBase touch
// state machine (0x002ea27c + ButtonBase::OnPress/OnRelease/OnLeave).
#include "display/base/control_layout.h"
#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_touch.h"
#include "tests/test_runner.h"

#ifdef HMI_HAVE_IGDT_TABLES
// 2.3 MB: never on the stack.
static IgdtRuntime g_rt;

static void test_control_creator_finds_appnoconn_and_info_nodes() {
    // vAppNoConnection (screen 0x04) control bars and VBtnInfo.
    const IgdtNode* bar1 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100164u);
    const IgdtNode* bar2 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100165u);
    const IgdtNode* btn_info = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100166u);
    CHECK(bar1 != 0);
    CHECK(bar2 != 0);
    CHECK(btn_info != 0);

    // vAppConnectInfo (screen 0x03) divider and Back button.
    const IgdtNode* divider = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x40100163u);
    const IgdtNode* btn_back = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x4010015du);
    CHECK(divider != 0);
    CHECK(btn_back != 0);
    CHECK(IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0xdeadbeefu) == 0);
    CHECK(IgdtFindNodeByTag(g_igdtTablesVw, 0xff, 0x40100166u) == 0);
    // A node of another screen is not found through screen 0x04.
    CHECK(IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x4010015du) == 0);
}

static void test_element_rects() {
    int x = -1, y = -1, w = -1, h = -1;
    // bar1: nine images stacked at (0, 386); union of their texture sizes.
    const IgdtNode* bar1 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100164u);
    CHECK(IgdtCalcNodeRect(g_igdtTablesVw, bar1, &x, &y, &w, &h));
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 386);
    CHECK_EQ(w, 20);
    CHECK_EQ(h, 74);
    // A text node: stock gives it bottom = top - 1 (zero height).
    const IgdtNode* txt = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x4510005au);
    CHECK(txt != 0);
    CHECK(IgdtCalcNodeRect(g_igdtTablesVw, txt, &x, &y, &w, &h));
    CHECK_EQ(y, 402);
    CHECK_EQ(h, 0);
    CHECK(!IgdtCalcNodeRect(g_igdtTablesVw, 0, &x, &y, &w, &h));
}

static void test_9part_composite_size_updates_runtime() {
    IgdtRuntimeInit(g_rt, g_igdtTablesVw);
    const IgdtNode* bar1 = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100164u);
    CHECK(bar1 != 0);
    // vAppNoConnection 0x0033a660: CreateComposite(4, 0x40100164, 0, 0x182, 800, 0x5e, ICF_9_PARTS).
    CHECK(IgdtApplyCompositeSize(g_igdtTablesVw, g_rt, bar1, 0, 0x182, 800, 0x5e, ICF_9_PARTS));
    // The composite's own group is not translated: the item's origin is
    // the (x, y) it was built with, so its displacement stays 0.
    int16_t gx = -1, gy = -1;
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x40100164u, &gx, &gy), IGDT_OK);
    CHECK_EQ(gx, 0);
    CHECK_EQ(gy, 0);

    // Upper centre: moved right of the 10 px corner, stretched to 780 px.
    int16_t px = -1, py = -1;
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x4210027cu, &px, &py), IGDT_OK);
    CHECK_EQ(px, 10);
    CHECK_EQ(py, 0x182);
    CHECK(g_rt.dyn_img[0x27c].matrix[0] == 39.0f);
    CHECK(g_rt.dyn_img[0x27c].matrix[4] == 1.0f);
    // Middle centre: below the upper row, 780 x 74.
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x4210027fu, &px, &py), IGDT_OK);
    CHECK_EQ(px, 10);
    CHECK_EQ(py, 0x182 + 10);
    CHECK(g_rt.dyn_img[0x27f].matrix[0] == 39.0f);
    CHECK(g_rt.dyn_img[0x27f].matrix[4] == 1.0f);
    // Lower right corner at (800 - 10, 0x182 + 0x5e - 10), unscaled.
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x42100283u, &px, &py), IGDT_OK);
    CHECK_EQ(px, 790);
    CHECK_EQ(py, 0x182 + 0x5e - 10);
    CHECK(g_rt.dyn_img[0x283].matrix[0] == 1.0f);
    // The upper left corner is only sized, never moved.
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x4210027bu, &px, &py), IGDT_OK);
    CHECK_EQ(px, 0);
    CHECK_EQ(py, 0x182);
}

static void test_lcr_composite_size_updates_runtime() {
    IgdtRuntimeInit(g_rt, g_igdtTablesVw);
    // vAppConnectInfo 0x00345a60: CreateComposite(3, 0x40100163, 0, 0x3d, 800, 0xc, ICF_LCR).
    // Parts: 8 x 12 at x 0, 14 x 12 at x 8, 8 x 12 at x 22.
    const IgdtNode* divider = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x40100163u);
    CHECK(divider != 0);
    CHECK(IgdtApplyCompositeSize(g_igdtTablesVw, g_rt, divider, 0, 0x3d, 800, 0xc, ICF_LCR));
    int16_t px = -1, py = -1;
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x42100269u, &px, &py), IGDT_OK);
    CHECK_EQ(px, 8);
    CHECK(g_rt.dyn_img[0x269].matrix[0] == 784.0f / 14.0f);
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x4210026au, &px, &py), IGDT_OK);
    CHECK_EQ(px, 792);
    CHECK_EQ(py, 0x3d);
    // A form whose part count does not match is refused (stock asserts).
    CHECK(!IgdtApplyCompositeSize(g_igdtTablesVw, g_rt, divider, 0, 0x3d, 800, 0xc, ICF_9_PARTS));
}

static void test_button_background_parts_move_their_groups() {
    IgdtRuntimeInit(g_rt, g_igdtTablesVw);
    // VBtnInfo's background (0x00100019): nine dynamic groups, each over a
    // state node; CreateImage gives each part its group as move id.
    const IgdtNode* btn = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100166u);
    CHECK(btn != 0);
    IgdtButtonNodes nodes;
    CHECK(IgdtParseButtonNodes(g_igdtTablesVw, btn, &nodes));
    CHECK_EQ(nodes.visibility_id, 0x01000044u);
    CHECK_EQ(nodes.touch_id, 0x40100167u);
    CHECK_EQ(nodes.dyn_id, 0x01000045u);
    CHECK(nodes.background != 0);
    CHECK_EQ(nodes.background->tag, 0x00100019u);
    CHECK(nodes.text_root != 0);
    CHECK_EQ(nodes.text_root->tag, 0x40100171u);
    // vAppNoConnection: CreateButton(4, 0x40100166, 0, 0x18c, 800, 0x54, ICF_9_PARTS).
    CHECK(IgdtApplyCompositeSize(g_igdtTablesVw, g_rt, nodes.background, 0, 0x18c, 800, 0x54, ICF_9_PARTS));
    int16_t gx = -1, gy = -1;
    // Upper right part group (images at x 30): moved by 790 - 30.
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x4010016au, &gx, &gy), IGDT_OK);
    CHECK_EQ(gx, 760);
    CHECK_EQ(gy, 0);
    // Lower left group (images at y 444): row starts at 0x18c + 0x54 - 20.
    CHECK_EQ(IgdtGetPosXY(g_rt, 0x4010016eu, &gx, &gy), IGDT_OK);
    CHECK_EQ(gx, 0);
    CHECK_EQ(gy, 0x18c + 0x54 - 20 - 444);
    // Middle centre images: scaled to (780 / 20, 44 / 20).
    CHECK(g_rt.dyn_img[0x294].matrix[0] == 39.0f);
    CHECK(g_rt.dyn_img[0x294].matrix[4] == 2.2f);
}

static void test_back_and_info_button_nodes() {
    IgdtRuntimeInit(g_rt, g_igdtTablesVw);
    // vAppConnectInfo Back (screen 0x03): visibility branch above a content
    // group whose background is a single LCR composite and that has no text.
    const IgdtNode* back = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x4010015du);
    CHECK(back != 0);
    IgdtButtonNodes nodes;
    CHECK(IgdtParseButtonNodes(g_igdtTablesVw, back, &nodes));
    CHECK_EQ(nodes.visibility_id, 0x01000041u);
    CHECK_EQ(nodes.touch_id, 0x4010015eu);
    CHECK_EQ(nodes.dyn_id, 0x01000042u);
    CHECK(nodes.background != 0);
    CHECK_EQ(nodes.background->tag, 0x4010015fu);
    CHECK(!nodes.background_is_branch);
    CHECK(nodes.text_root == 0);
    // vAppConnectInfo 0x00345a60: CreateButton(3, 0x4010015d, 0x2cf, 1, 0x3d, 0x3d, ICF_LCR).
    CHECK(IgdtApplyCompositeSize(g_igdtTablesVw, g_rt, nodes.background, 0x2cf, 1, 0x3d, 0x3d, ICF_LCR));

    // VBtnInfo (screen 0x04): plain background, one label (text id 28 =
    // 0x1c, the "Info" string vAppNoConnection sets).
    const IgdtNode* info = IgdtFindNodeByTag(g_igdtTablesVw, 0x04, 0x40100166u);
    CHECK(info != 0);
    CHECK(IgdtParseButtonNodes(g_igdtTablesVw, info, &nodes));
    CHECK(!nodes.background_is_branch);
    IgdtTextPart parts[4];
    CHECK_EQ(IgdtCollectButtonTexts(g_igdtTablesVw, nodes, parts, 4), 1);
    CHECK_EQ(parts[0].text_id, 28u);
}

static void test_status_line_comes_from_the_page_screens() {
    // Screen layers (scr_table+0): tuner 0, AppNoConn 1, AppConnectInfo 2.
    CHECK_EQ(IgdtScreenLayer(g_igdtTablesVw, 0xf9), 0);
    CHECK_EQ(IgdtScreenLayer(g_igdtTablesVw, 0x04), 1);
    CHECK_EQ(IgdtScreenLayer(g_igdtTablesVw, 0x03), 2);
    CHECK_EQ(IgdtScreenLayer(g_igdtTablesVw, 0xfe), 5);
    // 0x03 starts with an opaque full-screen background (0x02100000), which
    // hides the status line that 0x04 draws on the layer below.
    int x = -1, y = -1, w = -1, h = -1;
    const IgdtNode* bg = IgdtFindNodeByTag(g_igdtTablesVw, 0x03, 0x02100000u);
    CHECK(bg != 0);
    CHECK(IgdtCalcNodeRect(g_igdtTablesVw, bg, &x, &y, &w, &h));
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    CHECK_EQ(w, 800);
    CHECK_EQ(h, 480);
}
#endif

static void test_button_touch_state_machine() {
    // Normal press -> Pressed, no click yet.
    ButtonClickResult r = EvalButtonTouch(BTN_NORMAL, IGDT_TOUCH_PRESS);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_PRESSED);
    CHECK(!r.clicked);

    // Release inside -> Normal + clicked.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_RELEASE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(r.clicked);

    // Release with drag-out cancel (0x80000003) -> Normal, NOT clicked.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_OUTSIDE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(!r.clicked);

    // Release with page-change cancel (0x40000003) -> Normal, NOT clicked.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_PAGE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(!r.clicked);

    // Dragging out while still down (0x80000000, a move) -> Normal (OnLeave).
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_MOVE | IGDT_TOUCH_CANCEL_OUTSIDE);
    CHECK(r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_NORMAL);
    CHECK(!r.clicked);

    // A plain move changes nothing.
    r = EvalButtonTouch(BTN_PRESSED, IGDT_TOUCH_MOVE);
    CHECK(!r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_PRESSED);
    CHECK(!r.clicked);

    // A disabled button neither changes state nor clicks.
    r = EvalButtonTouch(BTN_DISABLED, IGDT_TOUCH_PRESS);
    CHECK(!r.state_changed);
    CHECK_EQ(r.new_state, (int)BTN_DISABLED);
    r = EvalButtonTouch(BTN_DISABLED, IGDT_TOUCH_RELEASE);
    CHECK(!r.state_changed);
    CHECK(!r.clicked);
}

static void test_composite_parts_lcr_and_uml() {
    // Three 10 x 10 parts all at the composite origin.
    CompositePart p[3];
    for (int i = 0; i < 3; ++i) {
        p[i].rel_x = 0;
        p[i].rel_y = 0;
        p[i].w = 10;
        p[i].h = 10;
        p[i].x_set = false;
        p[i].y_set = false;
    }
    CHECK(ComposeCompositeParts(ICF_LCR, 100, 30, p, 3));
    CHECK(!p[0].x_set);
    CHECK_EQ(p[0].h, 30);
    CHECK(p[1].x_set);
    CHECK_EQ(p[1].rel_x, 10);
    CHECK_EQ(p[1].w, 80);
    CHECK_EQ(p[2].rel_x, 90);
    CHECK_EQ(p[2].w, 10);

    for (int i = 0; i < 3; ++i) {
        p[i].rel_x = 0;
        p[i].rel_y = 0;
        p[i].w = 10;
        p[i].h = 10;
        p[i].x_set = false;
        p[i].y_set = false;
    }
    CHECK(ComposeCompositeParts(ICF_UML, 30, 100, p, 3));
    CHECK_EQ(p[0].w, 30);
    CHECK(p[1].y_set);
    CHECK_EQ(p[1].rel_y, 10);
    CHECK_EQ(p[1].h, 80);
    CHECK_EQ(p[2].rel_y, 90);
    CHECK(!ComposeCompositeParts(ICF_ONE_PART, 30, 100, p, 3));
    CHECK(!ComposeCompositeParts(7, 30, 100, p, 3));
}

int main() {
#ifdef HMI_HAVE_IGDT_TABLES
    RUN_TEST(test_control_creator_finds_appnoconn_and_info_nodes);
    RUN_TEST(test_element_rects);
    RUN_TEST(test_9part_composite_size_updates_runtime);
    RUN_TEST(test_lcr_composite_size_updates_runtime);
    RUN_TEST(test_button_background_parts_move_their_groups);
    RUN_TEST(test_back_and_info_button_nodes);
    RUN_TEST(test_status_line_comes_from_the_page_screens);
#endif
    RUN_TEST(test_button_touch_state_machine);
    RUN_TEST(test_composite_parts_lcr_and_uml);
    return test_summary();
}
