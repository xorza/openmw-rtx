#pragma once

#include <optional>

#include <components/rtx/scenedesc.hpp>

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
    /// @return where the surface went, or nothing where the cell holds no water — which everything
    ///         downstream reads as a depth that is never positive.
    /// The water surface a cell placed, and what it is made of.
    ///
    /// **The indices are here because nothing else can speak for them.** This mesh and this material
    /// were put into the scene directly rather than found by a walk, so a mark and sweep keyed on
    /// what the walk met would find them unreferenced and take them — with a placement still
    /// standing on them. Whoever places one has to tell the mirror to hold them.
    struct WaterSurface
    {
        float mLevel = 0.0f;
        Rtx::Index mMesh = Rtx::sNoIndex;
        Rtx::Index mMaterial = Rtx::sNoIndex;

        /// Where the quad stands, so whoever placed it can drop it again when the cell it belongs
        /// to leaves. One surface per cell: the sea is continuous, and a cell needs no more of it
        /// than its own footprint.
        Rtx::Index mInstance = Rtx::sNoIndex;
    };

    std::optional<WaterSurface> addWater(Rtx::SceneDesc& scene, const ESM::Cell& cell);
}
