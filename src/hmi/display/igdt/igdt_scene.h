// File name is ours; stock name unknown.
//
// IGDT scene walk: draws one screen's flattened elements (igdt_tables.h)
// through the GLES2 device layer (igdt_gl.h).
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_SCENE_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_SCENE_H

#include <stdint.h>

#include "display/igdt/igdt_tables.h"

// 0x0032072c: begins a frame and draws the images and text of screen
// screen_id in stock draw order (kind-5 text from the runtime text buffer,
// igdt_textbuf.h). Rects and runtime-driven state (page offsets, dynamic
// group translations, kind-8 nodes) are not drawn yet.
// Returns false (after one log line) when the screen id is unknown.
bool IgdtSceneDraw(const IgdtSkinTables& t, uint16_t screen_id);

#endif
