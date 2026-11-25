// Stock source: display/controls/controlparts/compositebackground.cpp
//
// STDPlusLogicalControlLibrary::CompositeBackground: a CompositeItem whose
// parts are all IgdtImageItems over state nodes; SetState selects the same
// state on every part (normal / pressed / disabled images). It is the
// ButtonBackground an IgdtButton is built with.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_COMPOSITEBACKGROUND_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_COMPOSITEBACKGROUND_H

#include <stdint.h>

#include "display/controls/controlparts/compositeitem.h"

namespace STDPlusLogicalControlLibrary {

// Stock interface STDPlusLogicalControlLibrary::ButtonBackground (file name
// unknown): what IgdtButton needs from its background.
class ButtonBackground {
public:
    virtual ~ButtonBackground() {}
    virtual void SetState(int state) = 0;
    // The background as an item (to re-parent and resize it).
    virtual ItemBase* GetItem() = 0;
};

// Not Q_OBJECT in stock (no moc data for it).
class CompositeBackground : public CompositeItem, public ButtonBackground {
public:
    CompositeBackground(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t visibility_id,
                        ITEM_COMBINATION_FORM form, const ItemContainer& items, uint32_t move_id);
    virtual ~CompositeBackground();

    virtual void SetState(int state);
    virtual ItemBase* GetItem() { return this; }
};

} // namespace STDPlusLogicalControlLibrary

#endif
