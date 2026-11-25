// Stock source: display/controls/controlparts/igdttextitem.cpp
//
// STDPlusLogicalControlLibrary::IgdtTextItem: one IGDT text (runtime text
// id = text_layout index) with optional glow images shown while pressed.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_IGDTTEXTITEM_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_IGDTTEXTITEM_H

#include <stdint.h>

#include "display/controls/controlbase/itembase.h"

namespace STDPlusLogicalControlLibrary {

// DEVIATION: stock derives from a text base class (ctor 0x002f9334) that
// also registers the item in a global list (0x00a0f590) and keeps three
// QStrings; ours derives from ItemBase.
class IgdtTextItem : public ItemBase {
    Q_OBJECT
public:
    // A text with no rect (pages add it with AddChild).
    explicit IgdtTextItem(unsigned text_id);
    // ControlCreator::CreateText. color_tag is the text's dynamic tag
    // (0 when static); glow_* are the composite text's glow images.
    IgdtTextItem(int32_t x, int32_t y, int32_t w, int32_t h, unsigned text_id, uint8_t font, uint32_t glow_l,
                 uint32_t glow_c, uint32_t glow_r, uint32_t color_tag, uint32_t move_id);
    virtual ~IgdtTextItem();

    unsigned GetTextId() const { return m_text_id; }
    uint8_t GetFont() const { return m_font; }
    uint32_t GetStringId() const { return m_str_id; }

    // Shows string str_id (re-translated on a language change).
    virtual void SetText(uint32_t str_id);
    // Shows a UTF-8 string.
    virtual void SetText(const char* utf8);
    void SetGlowIds(uint32_t glow_l, uint32_t glow_c, uint32_t glow_r);
    virtual void ShowGlow(bool show);
    virtual void SetColor(uint32_t argb);
    uint32_t GetColor() const { return m_color; }

private:
    uint32_t m_unused70;   // +0x70: always 0 from CreateText
    unsigned m_text_id;    // +0x74
    uint8_t m_font;        // +0x78
    uint32_t m_glow[3];    // +0x7c..+0x84
    uint32_t m_color_tag;  // +0x88
    uint32_t m_str_id;     // base +0x4c
    uint32_t m_color;      // ours
    // Ours: glow x before the first ShowGlow(false) moved it off screen.
    int16_t m_glow_x[3];
    bool m_glow_saved[3];
};

} // namespace STDPlusLogicalControlLibrary

#endif
