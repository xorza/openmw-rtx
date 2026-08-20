#ifndef OPENMW_COMPONENTS_RTXBRIDGE_WATERBUILDER_H
#define OPENMW_COMPONENTS_RTXBRIDGE_WATERBUILDER_H

namespace ESM
{
    struct Cell;
}

namespace Rtx
{
    class SceneDesc;
}

namespace RtxBridge
{
    /// Places a cell's water surface into `scene`, or nothing where the cell holds none.
    ///
    /// **The flag is the gate, not the value.** Every interior carries a water height whether or not
    /// it has water, so reading the height and taking its presence as the answer floods the several
    /// hundred dry rooms in the game. `ESM::Cell::hasWater` is the question, and it already answers
    /// it the right way: the flag for an interior, true for every exterior.
    ///
    /// **The sea is at zero everywhere out of doors.** Not one shipped exterior carries a height of
    /// its own, so Vvardenfell is one body of water and a cell needs no more than its own footprint
    /// of it. An interior's pool sits at the height its record names and spans what the room holds,
    /// which is as much as a flat quad can know about a cave — so this must be called after the
    /// cell's geometry, which is what gives it those bounds.
    void addWater(Rtx::SceneDesc& scene, const ESM::Cell& cell);
}

#endif
