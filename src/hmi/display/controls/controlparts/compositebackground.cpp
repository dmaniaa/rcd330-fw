// Stock source: display/controls/controlparts/compositebackground.cpp
#include "display/controls/controlparts/compositebackground.h"

namespace STDPlusLogicalControlLibrary {

// DEVIATION: stock asserts are_all_the_type<Image>(items)
// (compositebackground.cpp:72); ours takes the items as given and SetState
// reaches non-image items through ItemBase::SetDynIndex (a no-op).
// 0x002c85f0 CompositeBackground::CompositeBackground
CompositeBackground::CompositeBackground(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag,
                                         uint32_t visibility_id, ITEM_COMBINATION_FORM form,
                                         const ItemContainer& items, uint32_t move_id)
    : CompositeItem(x, y, w, h, tag, visibility_id, form, items, move_id) {}

// No origin tag: stock's destructor is not exported as a separate function.
CompositeBackground::~CompositeBackground() {}

// 0x002c8474 CompositeBackground::SetState
void CompositeBackground::SetState(int state) {
    const ItemContainer& items = GetItems();
    for (ItemContainer::const_iterator it = items.begin(); it != items.end(); ++it) {
        if (*it) (*it)->SetDynIndex(state);
    }
}

} // namespace STDPlusLogicalControlLibrary
