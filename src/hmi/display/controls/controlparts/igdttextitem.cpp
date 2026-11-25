// Stock source: display/controls/controlparts/igdttextitem.cpp
#include "display/controls/controlparts/igdttextitem.h"

#include "display/igdt/extend/igdt_wrapper.h"
#include "display/igdt/igdt_textbuf.h"   // IGDT_OK

namespace STDPlusLogicalControlLibrary {

namespace {

// ShowGlow(false) parks the glow images at this x.
const int16_t kGlowHiddenX = -2000;

} // namespace

// 0x002d02ac IgdtTextItem::IgdtTextItem
IgdtTextItem::IgdtTextItem(unsigned text_id)
    : ItemBase(0, 0, 0, 0, 0, 0),
      m_unused70(0),
      m_text_id(text_id),
      m_font(0),
      m_color_tag(0),
      m_str_id(0),
      m_color(0xffffffffu) {
    for (int i = 0; i < 3; ++i) {
        m_glow[i] = 0;
        m_glow_x[i] = 0;
        m_glow_saved[i] = false;
    }
    ShowGlow(false);
}

// 0x002d0348 IgdtTextItem::IgdtTextItem
IgdtTextItem::IgdtTextItem(int32_t x, int32_t y, int32_t w, int32_t h, unsigned text_id, uint8_t font,
                           uint32_t glow_l, uint32_t glow_c, uint32_t glow_r, uint32_t color_tag, uint32_t move_id)
    : ItemBase(x, y, w, h, 0, 0),
      m_unused70(0),
      m_text_id(text_id),
      m_font(font),
      m_color_tag(color_tag),
      m_str_id(0),
      m_color(0xffffffffu) {
    m_glow[0] = glow_l;
    m_glow[1] = glow_c;
    m_glow[2] = glow_r;
    for (int i = 0; i < 3; ++i) {
        m_glow_x[i] = 0;
        m_glow_saved[i] = false;
    }
    SetMoveId(move_id);
    ShowGlow(false);
}

// No origin tag: stock's destructor is not exported as a separate function.
IgdtTextItem::~IgdtTextItem() {}

// TODO(phase3): stock then measures the string (glyphs via 0x0031d1bc,
// extents via 0x00061984 with font & 0x7f) and calls SetSize(width, height).
// 0x002cf428 IgdtTextItem::SetText
void IgdtTextItem::SetText(uint32_t str_id) {
    m_str_id = str_id;
    hmi_wrapper_igdt_set_text_id(m_text_id, str_id);
}

// DEVIATION: stock converts with QString::fromUtf8 and calls its QString
// overload; ours hands the UTF-8 straight to the wrapper.
// 0x002cf4c8 IgdtTextItem::SetText
void IgdtTextItem::SetText(const char* utf8) {
    hmi_wrapper_igdt_set_text_utf8(m_text_id, utf8 ? utf8 : "");
}

// 0x002d0414 IgdtTextItem::SetGlowIds
void IgdtTextItem::SetGlowIds(uint32_t glow_l, uint32_t glow_c, uint32_t glow_r) {
    m_glow[0] = glow_l;
    m_glow[1] = glow_c;
    m_glow[2] = glow_r;
    for (int i = 0; i < 3; ++i) m_glow_saved[i] = false;
}

// DEVIATION: ShowGlow(true) in stock lays the glow out around the measured
// text (0x001cbb70 / 0x001cbabc / 0x001cb82c, needs text measurement:
// TODO(phase3)); ours puts the glow images back at the x they had before
// ShowGlow(false) parked them at -2000, i.e. the scene layout.
// 0x002cfff4 IgdtTextItem::ShowGlow
void IgdtTextItem::ShowGlow(bool show) {
    for (int i = 0; i < 3; ++i) {
        if (m_glow[i] == 0) continue;
        if (!show) {
            int16_t gx = 0, gy = 0;
            if (!m_glow_saved[i] && hmi_wrapper_igdt_get_xy(m_glow[i], &gx, &gy) == IGDT_OK && gx != kGlowHiddenX) {
                m_glow_x[i] = gx;
                m_glow_saved[i] = true;
            }
            hmi_wrapper_igdt_set_x(m_glow[i], kGlowHiddenX);
        } else if (m_glow_saved[i]) {
            hmi_wrapper_igdt_set_x(m_glow[i], m_glow_x[i]);
        }
    }
}

// TODO(phase3): stock sets the text colour through 0x000617b0 (IGDT text
// foreground, 0x00321020), which the runtime does not port yet; ours only
// records it.
// 0x002cf3f4 IgdtTextItem::SetColor
void IgdtTextItem::SetColor(uint32_t argb) {
    m_color = argb;
}

} // namespace STDPlusLogicalControlLibrary
