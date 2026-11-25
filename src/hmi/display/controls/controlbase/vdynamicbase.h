// Stock source: display/controls/controlbase/vdynamicbase.cpp
//
// A control's IGDT branch or state node: SetDynIndex selects the branch
// child (type 1) or the state (type 2). SetDynId derives the type from the
// tag kind (1 branch, 3 state) when none was given.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_VDYNAMICBASE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_VDYNAMICBASE_H

#include <stdint.h>

namespace STDPlusLogicalControlLibrary {

// DEVIATION: stock's m_pImpl_ block is held inline (see VMoveableBase).
class VDynamicBase {
public:
    enum DynType { DYN_NONE = 0, DYN_BRANCH = 1, DYN_STATE = 2 };

    VDynamicBase(uint32_t dyn_id, int dyn_type);
    virtual ~VDynamicBase();

    void SetDynId(uint32_t id);
    uint32_t GetDynId() const;
    void SetDynType(int type);
    int GetDynType() const;
    void SetDynIndex(int index);
    int GetDynIndex() const;

private:
    uint32_t m_dyn_id;
    int m_dyn_type;
    int m_dyn_index;
};

} // namespace STDPlusLogicalControlLibrary

#endif
