// Stock source: display/controls/controlbase/vmoveablebase.cpp
//
// Position of a control relative to its parent (relX/relY) and the IGDT
// dynamic group ("move id") that translates its scene nodes. SetX/SetY
// store the relative position and, when there is a move id, set that
// group's x/y to the control's displacement from where the scene placed it.
#ifndef RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_VMOVEABLEBASE_H
#define RCD330_HMI_DISPLAY_CONTROLS_CONTROLBASE_VMOVEABLEBASE_H

#include <stdint.h>

namespace STDPlusLogicalControlLibrary {

// DEVIATION: stock keeps moveId/relX/relY in a heap m_pImpl_ block and logs
// when it is NULL; ours holds them inline (the block is never NULL).
class VMoveableBase {
public:
    VMoveableBase(int32_t rel_x, int32_t rel_y, uint32_t move_id);
    virtual ~VMoveableBase();

    void SetMoveId(uint32_t move_id);
    uint32_t GetMoveId() const;
    void SetRelaX(int32_t x);
    void SetRelaY(int32_t y);
    int32_t GetRelaX() const;
    int32_t GetRelaY() const;
    virtual void SetX(int32_t rel_x);
    virtual void SetY(int32_t rel_y);
    void SetPos(int32_t rel_x, int32_t rel_y);

protected:
    // The group offset SetX writes is MoveAbsX() - MoveBaseX(): the owner's
    // absolute x minus (its scene x + its parent's displacement). Names ours
    // (stock vtable slots +0x20/+0x10 and +0x24/+0x14).
    virtual int32_t MoveAbsX() const = 0;
    virtual int32_t MoveBaseX() const = 0;
    virtual int32_t MoveAbsY() const = 0;
    virtual int32_t MoveBaseY() const = 0;

private:
    uint32_t m_move_id;
    int32_t m_rel_x;
    int32_t m_rel_y;
};

} // namespace STDPlusLogicalControlLibrary

#endif
