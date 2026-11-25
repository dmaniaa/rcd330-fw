// File name is ours; stock name unknown.
// Host tests for the IGDT touch queue and hit test (display/igdt/igdt_touch.h)
// against the generated 5624 scene tables.
#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_touch.h"
#include "tests/test_runner.h"

#include <vector>

static void test_queue_full_drops_without_overflow() {
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    IgdtTouchEvent ev = {10, 10, IGDT_TOUCH_MOVE};
    for (int i = 0; i < 60; ++i) CHECK(IgdtTouchEnqueue(ts, ev));
    CHECK(!IgdtTouchEnqueue(ts, ev));
}

#ifdef HMI_HAVE_IGDT_TABLES
struct RecordedTouch {
    uint32_t tag;
    int32_t x, y;
    uint32_t type;
};

static void RecordCb(uint32_t tag, const IgdtTouchEvent& ev, void* ctx) {
    std::vector<RecordedTouch>* out = static_cast<std::vector<RecordedTouch>*>(ctx);
    RecordedTouch r = {tag, ev.x, ev.y, ev.type};
    out->push_back(r);
}

// The runtime is 2.3 MB: static, not on the stack.
static IgdtRuntime g_rt;

// VBtnInfo on screen 0x04 (vAppNoConnection 0x0033a660: 0, 0x18c, 800 x 0x54).
// Its only touch path (root 6, steps 2 0 1 0 0xff) ends at dyn group
// 0x40100167, below 0x40100166 and branch 0x01000044 (child 1 selected).
static const uint32_t kInfoTouchTag = 0x40100167u;

static void ShowAppNoConn() {
    IgdtRuntimeInit(g_rt, g_igdtTablesVw);
    CHECK_EQ(IgdtLayerShowScreen(g_rt, g_igdtTablesVw, 0x04), IGDT_OK);
    CHECK_EQ(IgdtSetTouchArea(g_rt, kInfoTouchTag, 0, 396, 800, 84), IGDT_OK);
}

static void test_appnoconn_info_button_hit_and_release() {
    ShowAppNoConn();
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEvent up = {400, 438, IGDT_TOUCH_RELEASE};
    CHECK(IgdtTouchEnqueue(ts, down));
    CHECK(IgdtTouchEnqueue(ts, up));
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);

    CHECK_EQ(log.size(), 2u);
    if (log.size() != 2u) return;
    CHECK_EQ(log[0].tag, kInfoTouchTag);
    CHECK_EQ(log[0].type, (uint32_t)IGDT_TOUCH_PRESS);
    CHECK_EQ(log[0].x, 400);
    CHECK_EQ(log[0].y, 438);
    CHECK_EQ(log[1].tag, kInfoTouchTag);
    CHECK_EQ(log[1].type, (uint32_t)IGDT_TOUCH_RELEASE);
    CHECK(ts.captured == 0);
    CHECK_EQ(ts.captured_screen, IGDT_SCREEN_NONE);
}

static void test_press_outside_and_stray_events_hit_nothing() {
    ShowAppNoConn();
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    // Moves and releases without a captured node are dropped; a press above
    // the button (and its default 132 px wide area) hits no path.
    IgdtTouchEvent move = {400, 438, IGDT_TOUCH_MOVE};
    IgdtTouchEvent up = {400, 438, IGDT_TOUCH_RELEASE};
    IgdtTouchEvent down_out = {400, 100, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, move);
    IgdtTouchEnqueue(ts, up);
    IgdtTouchEnqueue(ts, down_out);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 0u);
    CHECK_EQ(ts.count, 0u);

    // Stock hit rects include the right / bottom edge (x0 + w, y0 + h).
    IgdtTouchEvent edge = {800, 480, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, edge);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 1u);
}

static void test_inactive_branch_child_rejects_path() {
    ShowAppNoConn();
    // The path steps through child 1 of branch 0x01000044; with child 0
    // selected it must not hit.
    CHECK_EQ(IgdtSetBranch(g_rt, 0x01000044u, 0), IGDT_OK);
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;
    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 0u);
    CHECK(ts.captured == 0);
}

static void test_dynamic_group_offset_moves_hit_rect() {
    ShowAppNoConn();
    // Dyn group 0x40100166 lies on the path: its RAM position shifts the touch area.
    CHECK_EQ(IgdtSetPosXY(g_rt, 0x40100166u, 0, -100), IGDT_OK);
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;
    IgdtTouchEvent down_old = {400, 470, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, down_old);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 0u);
    IgdtTouchEvent down_new = {400, 338, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, down_new);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 1u);
    if (log.size() == 1u) CHECK_EQ(log[0].tag, kInfoTouchTag);
}

static void test_branch_terminal_hits_selected_child_rect() {
    // Screen 0x06 path (9, 2, 0xff) ends at static branch 0x01000099; its
    // selected child 1 is static group 0x00100062 (clip 722, 415, 55 x 55).
    IgdtRuntimeInit(g_rt, g_igdtTablesVw);
    CHECK_EQ(IgdtLayerShowScreen(g_rt, g_igdtTablesVw, 0x06), IGDT_OK);
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;
    IgdtTouchEvent down = {750, 440, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 1u);
    if (log.size() == 1u) CHECK_EQ(log[0].tag, 0x01000099u);
    CHECK_EQ(ts.captured_screen, 0x06);
}

static void test_drag_outside_cancels_release() {
    // Review Focus 3: sliding finger off button sets 0x80000000 cancel bit.
    ShowAppNoConn();
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEvent move_out = {400, 100, IGDT_TOUCH_MOVE};
    IgdtTouchEvent up_out = {400, 100, IGDT_TOUCH_RELEASE};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchEnqueue(ts, move_out);
    IgdtTouchEnqueue(ts, up_out);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);

    CHECK_EQ(log.size(), 3u);
    if (log.size() != 3u) return;
    CHECK_EQ(log[1].tag, kInfoTouchTag);
    CHECK((log[1].type & IGDT_TOUCH_CANCEL_OUTSIDE) != 0);
    CHECK_EQ(log[2].type, (uint32_t)(IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_OUTSIDE));
}

static void test_drag_back_inside_stays_cancelled() {
    // STOCK-QUIRK: the captured rect collapses to (0, 0, -1, -1), so a
    // release back on the button still carries the cancel bit.
    ShowAppNoConn();
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;
    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEvent move_out = {400, 100, IGDT_TOUCH_MOVE};
    IgdtTouchEvent up_in = {400, 438, IGDT_TOUCH_RELEASE};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchEnqueue(ts, move_out);
    IgdtTouchEnqueue(ts, up_in);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 3u);
    if (log.size() == 3u) CHECK_EQ(log[2].type, (uint32_t)(IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_OUTSIDE));
}

static void test_page_insert_cancels_captured_touch() {
    // Review Focus 4: inserting screen while touch is captured sets 0x40000000.
    ShowAppNoConn();
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;

    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 1u);

    // Another screen leaves the capture alone; the captured one cancels it.
    IgdtTouchInvalidateForScreen(ts, 0x05);
    CHECK_EQ(ts.cap_x1, 800);
    IgdtTouchInvalidateForScreen(ts, 0x04);
    IgdtTouchEvent up = {400, 438, IGDT_TOUCH_RELEASE};
    IgdtTouchEnqueue(ts, up);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 2u);
    if (log.size() == 2u) CHECK_EQ(log[1].type, (uint32_t)(IGDT_TOUCH_RELEASE | IGDT_TOUCH_CANCEL_PAGE));
    CHECK(ts.captured == 0);
}

static void test_page_insert_all_screens_cancels() {
    ShowAppNoConn();
    IgdtTouchState ts;
    IgdtTouchInit(ts);
    std::vector<RecordedTouch> log;
    IgdtTouchEvent down = {400, 438, IGDT_TOUCH_PRESS};
    IgdtTouchEvent move = {401, 438, IGDT_TOUCH_MOVE};
    IgdtTouchEnqueue(ts, down);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    IgdtTouchInvalidateForScreen(ts, IGDT_SCREEN_NONE);
    IgdtTouchEnqueue(ts, move);
    IgdtTouchProcessQueue(ts, g_igdtTablesVw, g_rt, RecordCb, &log);
    CHECK_EQ(log.size(), 2u);
    if (log.size() == 2u) CHECK_EQ(log[1].type, (uint32_t)(IGDT_TOUCH_MOVE | IGDT_TOUCH_CANCEL_PAGE));
}
#endif

int main() {
    RUN_TEST(test_queue_full_drops_without_overflow);
#ifdef HMI_HAVE_IGDT_TABLES
    RUN_TEST(test_appnoconn_info_button_hit_and_release);
    RUN_TEST(test_press_outside_and_stray_events_hit_nothing);
    RUN_TEST(test_inactive_branch_child_rejects_path);
    RUN_TEST(test_dynamic_group_offset_moves_hit_rect);
    RUN_TEST(test_branch_terminal_hits_selected_child_rect);
    RUN_TEST(test_drag_outside_cancels_release);
    RUN_TEST(test_drag_back_inside_stays_cancelled);
    RUN_TEST(test_page_insert_cancels_captured_touch);
    RUN_TEST(test_page_insert_all_screens_cancels);
#endif
    return test_summary();
}
