// Stock source: display/controls/controlparts/igdtbutton.cpp
//
// STDPlusLogicalControlLibrary::IgdtButton: the standard touch button built
// by ControlCreator::CreateButton: a ButtonBackground (state images), text
// items (white, grey when disabled, glow while pressed) and an optional
// dynamic branch/state (dyn id) switched with the button state.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_IGDTBUTTON_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_IGDTBUTTON_H

#include <stdint.h>

#include <vector>

#include "display/controls/controlbase/igdtbuttonbase.h"
#include "display/controls/controlbase/vdynamicbase.h"

namespace STDPlusLogicalControlLibrary {

class ButtonBackground;
class IgdtTextItem;

// DEVIATION: stock keeps the texts in an ItemContainer and dynamic_casts
// each to IgdtTextItem; ours keeps them typed.
typedef std::vector<IgdtTextItem*> TextItemContainer;

class IgdtButton : public IgdtButtonBase {
    Q_OBJECT
public:
    // cursor: the focus frame item (VCursorLogic), NULL from CreateButton.
    IgdtButton(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t visibility_id, uint32_t touch_id,
               uint32_t dyn_id, ButtonBackground* background, const TextItemContainer& texts, ItemBase* cursor,
               uint32_t move_id);
    virtual ~IgdtButton();

    // Resizes the background and the touch area.
    virtual void SetSize(int32_t w, int32_t h);
    // 0 normal, 1 pressed, 2 disabled: background state, text glow/colour
    // and the dynamic branch/state all follow.
    virtual void SetState(int state);
    virtual void SetSelected(bool selected);
    void SetBgStatus(int state);
    void SetText(int idx, uint32_t str_id);
    void SetText(int idx, const char* utf8);
    void SetTextColor(uint32_t normal, uint32_t disabled);
    IgdtTextItem* GetTextItem(int idx) const;
    void AddTextItem(IgdtTextItem* item);
    int GetTextItemCount() const { return (int)m_texts.size(); }
    ButtonBackground* GetBackground() const { return m_background; }
    uint32_t GetTag() const { return m_tag; }
    uint32_t GetDynId() const { return m_dynamic.GetDynId(); }

private:
    uint32_t m_tag;
    ButtonBackground* m_background;   // +0x9c
    ItemBase* m_cursor;               // +0xa0
    VDynamicBase m_dynamic;           // +0xa4
    TextItemContainer m_texts;        // +0xac
};

} // namespace STDPlusLogicalControlLibrary

#endif
