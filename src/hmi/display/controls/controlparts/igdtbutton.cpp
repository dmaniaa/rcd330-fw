// Stock source: display/controls/controlparts/igdtbutton.cpp
#include "display/controls/controlparts/igdtbutton.h"

#include "display/controls/controlparts/compositebackground.h"
#include "display/controls/controlparts/igdttextitem.h"
#include "service/osal/osal_api.h"

#define IGDTBUTTON_CHECK_LOG(what, func, line)                                                          \
    do {                                                                                                \
        log_simple("ERROR", " ==== HMI ==== ERROR: The result of %s is false !! (%s ->: %d)", what, func, \
                   line);                                                                               \
        printlog(3, "ERROR", "display/controls/controlparts/igdtbutton.cpp", func, line,                \
                 " ==== HMI ==== ERROR: The result of %s is false !! (%s ->: %d)", what, func, line);   \
    } while (0)

namespace STDPlusLogicalControlLibrary {

// 0x002ce510 IgdtButton::IgdtButton
IgdtButton::IgdtButton(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t visibility_id,
                       uint32_t touch_id, uint32_t dyn_id, ButtonBackground* background,
                       const TextItemContainer& texts, ItemBase* cursor, uint32_t move_id)
    : IgdtButtonBase(x, y, w, h, 0, 0, touch_id),
      m_tag(tag),
      m_background(background),
      m_cursor(cursor),
      m_dynamic(dyn_id, VDynamicBase::DYN_NONE),
      m_texts(texts) {
    Visibility().SetVisibilityId(visibility_id);
    SetMoveId(move_id);
    if (m_background && m_background->GetItem()) m_background->GetItem()->SetParent(this);
    for (TextItemContainer::iterator it = m_texts.begin(); it != m_texts.end(); ++it) {
        if (*it) (*it)->SetParent(this);
    }
    SetSize(w, h);
    ButtonBase::SetState(STATE_NORMAL);
    if (m_background) m_background->SetState(STATE_NORMAL);
    m_dynamic.SetDynIndex(0);
    SetSelected(false);
    for (TextItemContainer::iterator it = m_texts.begin(); it != m_texts.end(); ++it) {
        if (*it) (*it)->SetColor(GetTextColor());
    }
}

// No origin tag: stock's destructor is not exported as a separate function.
IgdtButton::~IgdtButton() {}

// 0x002cd438 IgdtButton::SetSize
void IgdtButton::SetSize(int32_t w, int32_t h) {
    ItemBase::SetSize(w, h);
    if (m_background && m_background->GetItem()) m_background->GetItem()->SetSize(w, h);
    SetTouchArea(GetAbsX(), GetAbsY(), w, h);
    if (IsSelected()) SetSelected(true);
}

// TODO(phase3): stock also tells the VCursorLogic singleton (0x009704d4,
// slot +0x1c) when a selected button becomes disabled.
// 0x002cd50c IgdtButton::SetState
void IgdtButton::SetState(int state) {
    ButtonBase::SetState(state);
    if (m_background) m_background->SetState(state);
    for (TextItemContainer::iterator it = m_texts.begin(); it != m_texts.end(); ++it) {
        if (!*it) {
            IGDTBUTTON_CHECK_LOG("txt", "SetState", 0x134);
            break;
        }
        (*it)->ShowGlow(state == STATE_PRESSED);
        (*it)->SetColor(state == STATE_DISABLED ? GetDisabledTextColor() : GetTextColor());
    }
    m_dynamic.SetDynIndex(state);
    if (state == STATE_DISABLED && IsSelected()) SetSelected(false);
}

// TODO(phase3): with a cursor item stock moves it onto the button (selected)
// or to x = -10000; CreateButton passes no cursor, so ours skips that.
// 0x002cd8fc IgdtButton::SetSelected
void IgdtButton::SetSelected(bool selected) {
    ButtonBase::SetSelected(selected);
    if (m_cursor && !selected) m_cursor->SetRelX(-10000);
    emit SigFocus(this, selected);
}

// 0x002cda98 IgdtButton::SetBgStatus
void IgdtButton::SetBgStatus(int state) {
    if (!m_background) {
        IGDTBUTTON_CHECK_LOG("m_pBackground", "SetBgStatus", 0x1de);
        return;
    }
    m_background->SetState(state);
}

// 0x002cdbcc IgdtButton::SetText
void IgdtButton::SetText(int idx, uint32_t str_id) {
    IgdtTextItem* t = GetTextItem(idx);
    if (t) t->SetText(str_id);
}

// No origin tag: stock's other SetText overloads take a QString (at VAs
// 2cdcbc and 2cddb0); ours takes UTF-8.
void IgdtButton::SetText(int idx, const char* utf8) {
    IgdtTextItem* t = GetTextItem(idx);
    if (t) t->SetText(utf8);
}

// 0x002cdf9c IgdtButton::SetTextColor
void IgdtButton::SetTextColor(uint32_t normal, uint32_t disabled) {
    SetTextColors(normal, disabled);
    for (TextItemContainer::iterator it = m_texts.begin(); it != m_texts.end(); ++it) {
        if (*it) (*it)->SetColor(GetState() == STATE_DISABLED ? GetDisabledTextColor() : GetTextColor());
    }
}

// 0x002ce0ac IgdtButton::GetTextItem
IgdtTextItem* IgdtButton::GetTextItem(int idx) const {
    if (idx < 0 || (size_t)idx >= m_texts.size()) {
        IGDTBUTTON_CHECK_LOG("((size_t)idx < m_TextItems.size())", "GetTextItem", 0x2a9);
        return 0;
    }
    return m_texts[idx];
}

// 0x002ce164 IgdtButton::AddTextItem
void IgdtButton::AddTextItem(IgdtTextItem* item) {
    m_texts.push_back(item);
    if (item) item->SetParent(this);
}

} // namespace STDPlusLogicalControlLibrary
