// File name is ours; stock name unknown.
//
// IGDT touch input: the 60-entry event ring buffer (0x00df28d4) that
// hmi_wrapper_igdt_touch_event fills, and the hit test that drains it
// (0x00324b90). A press walks the touch paths of every shown screen, top
// layer first, and captures the first node whose touch rect contains the
// point; later events go to that node until the release. Portable and
// host-testable: no GL, no locking (the wrapper holds the IGDT mutex).
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_TOUCH_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_TOUCH_H

#include <stdint.h>

#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_tables.h"

// Event type: low byte the action, high bits the cancel flags the hit test
// adds for a captured node. IgdtButtonBase::TouchEventHandler (0x002ea27c)
// clicks only on a release without either flag.
enum {
    IGDT_TOUCH_MOVE = 0,
    IGDT_TOUCH_PRESS = 1,
    IGDT_TOUCH_RELEASE = 3,
    IGDT_TOUCH_CANCEL_OUTSIDE = 0x80000000u,   // left the captured rect
    IGDT_TOUCH_CANCEL_PAGE = 0x40000000u       // the captured screen was re-inserted
};

enum { IGDT_TOUCH_QUEUE_LEN = 0x3c };

// Stock events are 0x1c bytes: x, y, type, then four words that the
// callback (0x00063298) passes as 0. Only x, y, type carry data.
struct IgdtTouchEvent {
    int32_t x;
    int32_t y;
    uint32_t type;
};

typedef void (*IgdtTouchCallback)(uint32_t node_tag, const IgdtTouchEvent& ev, void* ctx);

struct IgdtTouchState {
    IgdtTouchEvent queue[IGDT_TOUCH_QUEUE_LEN];  // 0x00df28d4
    uint32_t count;                 // 0x00df28b8
    uint32_t tail;                  // 0x00df28bc next write slot
    uint32_t head;                  // 0x00df28c0 next read slot
    const IgdtNode* captured;       // 0x00df28c4 node of the current press, 0 = none
    int16_t cap_x0, cap_y0;         // 0x00df28c8 captured rect, inclusive edges
    int16_t cap_x1, cap_y1;         // 0x00df28cc
    uint8_t captured_screen;        // 0x00df28d0 screen of the captured node
};

// Empty queue, nothing captured.
void IgdtTouchInit(IgdtTouchState& ts);

// Appends ev; false (ev dropped) when the queue already holds 60 events.
bool IgdtTouchEnqueue(IgdtTouchState& ts, const IgdtTouchEvent& ev);

// The part of hmi_wrapper_igdt_insert_page (0x000610a8) that touches the
// queue: when a node of screen_id (any screen for IGDT_SCREEN_NONE) is
// captured, its rect becomes the sentinel (0, 0, -2, -2), so the next event
// for it carries IGDT_TOUCH_CANCEL_PAGE.
void IgdtTouchInvalidateForScreen(IgdtTouchState& ts, uint8_t screen_id);

// Drains the queue, calling cb (when not NULL) with the captured or newly
// hit node's tag for every event that reaches a node.
void IgdtTouchProcessQueue(IgdtTouchState& ts, const IgdtSkinTables& t, const IgdtRuntime& rt,
                           IgdtTouchCallback cb, void* ctx);

#endif
