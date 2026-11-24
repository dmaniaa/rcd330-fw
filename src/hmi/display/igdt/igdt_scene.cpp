// File name is ours; stock name unknown.
#include "display/igdt/igdt_scene.h"

#include "display/igdt/igdt_gl.h"
#include "display/igdt/igdt_strings.h"
#include "display/igdt/igdt_text.h"
#include "display/igdt/igdt_textbuf.h"
#include "service/osal/osal_api.h"

namespace {

const uint16_t kScreenW = 800;   // 0x00a0f94c
const uint16_t kScreenH = 480;   // 0x00a0f94e

// Origin 0, opaque, no tint, identity transform, full-screen clip.
// 0x0031d684 ResetState
void ResetState(IgdtDrawState& s) {
    s.x = 0;
    s.y = 0;
    s.alpha = 0xff;
    s.tint = 0xffffffffu;
    s.flags = 0;
    for (int i = 0; i < 9; ++i) s.matrix[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    s.clip_x0 = 0;
    s.clip_y0 = 0;
    s.clip_x1 = (int16_t)s.screen_w;
    s.clip_y1 = (int16_t)s.screen_h;
}

// 0x0031daa4 Translate
void Translate(IgdtDrawState& s, int16_t dx, int16_t dy) {
    s.x = (int16_t)(s.x + dx);
    s.y = (int16_t)(s.y + dy);
}

// Alpha 0 hides; 0xff keeps; otherwise alpha = alpha * a >> 8.
// 0x0031db3c ApplyAlpha
void ApplyAlpha(IgdtDrawState& s, uint8_t a) {
    s.flags &= (uint8_t)~IGDT_STATE_TRANSPARENT;
    if (a == 0) {
        s.alpha = 0;
        s.flags |= IGDT_STATE_TRANSPARENT;
        return;
    }
    if (a == 0xff) return;
    s.alpha = (uint8_t)((unsigned)s.alpha * a >> 8);
    if (s.alpha == 0) s.flags |= IGDT_STATE_TRANSPARENT;
}

// The generator already intersected the group clips (0x0031da6c) into one
// screen rect; w == 0 && h == 0 is the empty clip.
// 0x0031da6c ApplyClip
void ApplyClip(IgdtDrawState& s, const IgdtClipRect& c) {
    if (c.w == 0 && c.h == 0) {
        s.flags |= IGDT_STATE_CLIPPED;
        return;
    }
    s.clip_x0 = c.x;
    s.clip_y0 = c.y;
    s.clip_x1 = (int16_t)(c.x + c.w);
    s.clip_y1 = (int16_t)(c.y + c.h);
}

// Record pivot (img record +0x30, txt record +0x34); zero in every 0x04
// record. TODO(phase2): generate it with the record matrix.
// 0x0031db90 SetPivot
void SetPivot(IgdtDrawState& s, float x, float y) {
    s.pivot_x = x;
    s.pivot_y = y;
}

// Font style and the three align fields of the text_layout +8 byte.
// 0x0031dfac SetTextStyle
void SetTextStyle(IgdtDrawState& s, uint8_t font, uint8_t align) {
    s.text_align = (uint8_t)((s.text_align & 0xc0) | (align & 3) | ((align >> 4) & 3) << 2 | (align >> 6) << 4);
    s.font = font;
}

// Static path; the static tables hold the initial record values.
// 0x0031f0a0 DrawImage
void DrawImage(const IgdtSkinTables& t, const IgdtElement& e) {
    IgdtDrawState& s = IgdtCurrentState();
    ResetState(s);
    // TODO(phase2): per-screen offset (DAT_00a0fa34 + layer) and dynamic
    // group / branch translations from RAM (0x0031daa4 calls in 0x0032072c
    // and 0x003202c4); static group alpha/matrix/pivot (0x003202c4) and the
    // record's own matrix/pivot (+0x0c / +0x30).
    Translate(s, e.x, e.y);
    ApplyAlpha(s, e.alpha);
    if (e.clip >= 0 && e.clip < t.clip_count) ApplyClip(s, t.clips[e.clip]);
    SetPivot(s, 0.0f, 0.0f);
    // Called even when s.flags != 0: stock 0x00326ee8 still updates the
    // scissor and MVP before its flags check (no pixel effect).
    IgdtDrawImage(t, e);
}

// Kind 4 reads the engineering table at e.str_id, kind 5 the runtime text
// buffer at e.rt_off (filled by page code via IgdtSetRuntimeText). Static
// path, like DrawImage.
// 0x0031f41c DrawText
void DrawText(const IgdtSkinTables& t, const IgdtElement& e) {
    IgdtDrawState& s = IgdtCurrentState();
    ResetState(s);
    // TODO(phase2): the same RAM translations as DrawImage, and the
    // record matrix +0x10 (0x0031df9c).
    Translate(s, e.x, e.y);
    ApplyAlpha(s, e.alpha);
    SetTextStyle(s, e.font, e.align);
    s.fg_color = e.color;      // 0x0031dfec
    s.bg_color = e.bg_color;   // 0x0031dffc
    if (e.clip >= 0 && e.clip < t.clip_count) ApplyClip(s, t.clips[e.clip]);
    const IgdtWchar* text;
    IgdtWchar eng[0x141];
    if (e.str_id == IGDT_STR_RUNTIME) {
        text = IgdtRuntimeText(t, e.rt_off);
    } else {
        // Stock reads the UTF-32 table in place; ours is generated as UTF-8.
        IgdtUtf8ToUtf32(IgdtGetEngString(e.str_id), eng, sizeof(eng) / sizeof(eng[0]));
        text = eng;
    }
    if (!text) return;
    SetPivot(s, 0.0f, 0.0f);
    IgdtDrawText(e, text);
}

} // namespace

// 0x0032072c IgdtSceneDraw
bool IgdtSceneDraw(const IgdtSkinTables& t, uint16_t screen_id) {
    const IgdtScreen* screen = 0;
    for (uint16_t i = 0; i < t.screen_count; ++i) {
        if (t.screens[i].id == screen_id) screen = &t.screens[i];
    }
    if (!screen) {
        log_simple("IGDT", "IGDT: unknown screen 0x%02x\n", screen_id);
        return false;
    }
    IgdtDrawState& s = IgdtCurrentState();
    s.screen_w = kScreenW;
    s.screen_h = kScreenH;
    ResetState(s);
    // TODO(phase2): stock walks the layer's screen list and redraws only the
    // dirty rects (0x0031e69c / 0x0031ecc8), clipping each pass to its rect.
    IgdtBeginFrame();
    for (uint16_t i = 0; i < screen->elem_count; ++i) {
        const IgdtElement& e = t.elements[screen->elem_first + i];
        if (e.type == IGDT_ELEM_IMG) {
            DrawImage(t, e);
        } else if (e.type == IGDT_ELEM_TXT) {
            DrawText(t, e);
        }
        // TODO(phase2): IGDT_ELEM_RECT (0x0031f8e4 kind 7 -> 0x0032749c).
    }
    return true;
}
