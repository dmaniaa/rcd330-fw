// File name is ours; stock name unknown.
//
// IGDT scene walk: draws the screens of the 29 display layers by walking
// the live scene graph (igdt_tables.h) with the dynamic runtime state
// (igdt_runtime.h) through the GLES2 device layer (igdt_gl.h).
#ifndef RCD330_HMI_DISPLAY_IGDT_IGDT_SCENE_H
#define RCD330_HMI_DISPLAY_IGDT_IGDT_SCENE_H

#include <stdint.h>

#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_tables.h"

// 0x0032072c: begins a frame and draws the screen of every non-empty layer,
// layer 0 first, in stock node order: groups and branches (0x003202c4) with
// their translation, tint, alpha, clip, matrix and pivot, the selected child
// of branch / state nodes, images, text (kind-5 from the runtime text buffer,
// igdt_textbuf.h) and kind-7 rects. False when no layer shows a screen.
bool IgdtSceneDrawLayers(const IgdtSkinTables& t, const IgdtRuntime& rt);

#endif
