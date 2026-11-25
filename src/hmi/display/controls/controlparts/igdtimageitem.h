// File name is ours; stock name unknown (class name is stock).
//
// STDPlusLogicalControlLibrary::IgdtImageItem: one image node, or the image
// children of a state node (dynamic id = the state). Moving it moves its
// move-id group when it has one, else each image to its new absolute
// position; resizing scales each image to size / original size.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_IGDTIMAGEITEM_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLPARTS_IGDTIMAGEITEM_H

#include <stdint.h>

#include <vector>

#include "display/controls/controlbase/itembase.h"
#include "display/controls/controlbase/vdynamicbase.h"

namespace STDPlusLogicalControlLibrary {

// DEVIATION: stock derives from an image base class (ctor 0x002f9800) that
// holds the VDynamicBase (type state); ours derives from ItemBase and holds
// the VDynamicBase as a member.
class IgdtImageItem : public ItemBase {
    Q_OBJECT
public:
    // A single image tag.
    IgdtImageItem(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t tag, uint32_t move_id);
    // A state node's images (dyn_id = the state, e.g. 0x030000bf).
    IgdtImageItem(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t dyn_id, const uint32_t* tags, int count,
                  uint32_t move_id);
    virtual ~IgdtImageItem();

    virtual void SetRelX(int32_t rel_x);
    virtual void SetRelY(int32_t rel_y);
    virtual void SetSize(int32_t w, int32_t h);

    // Selects the state image (no-op without a dynamic id).
    virtual void SetDynIndex(int index) { m_dynamic.SetDynIndex(index); }
    const std::vector<uint32_t>& GetTags() const { return m_tags; }

private:
    VDynamicBase m_dynamic;        // +0x40
    std::vector<uint32_t> m_tags;  // count +0x58, array +0x5c
};

} // namespace STDPlusLogicalControlLibrary

#endif
