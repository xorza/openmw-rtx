#pragma once

#include <cstdint>

#include <osg/Vec3f>

#include <components/esm/refid.hpp>
#include <components/esm3/refnum.hpp>

// What a walk of the content files says of one reference, apart from `objectstorage.hpp` so that the
// game's interfaces can name it without the storage.

namespace Terrain
{
    /// No gate: the reference stands by the content files and by what the game says of it.
    inline constexpr std::uint32_t sNoGate = ~0u;

    /// What the game says of a gate: whether the references behind it stand in the distance.
    enum class GateState : std::uint8_t
    {
        /// Not yet evaluated, and so nothing behind it stands: a stage the game has not decided on
        /// is more often down than up, and a gate is told before the frame that reads it.
        Unknown,

        Open,
        Closed,

        /// The script's answer rests on something only an active cell has — an activation that
        /// takes the reference down, another reference's state, an instruction the evaluation does
        /// not model — so each reference stands by what the game says of it, as an ungated one
        /// does.
        Undecided,
    };

    /// One reference a chunk stands, reduced to what the paging needs of it.
    struct PagedCellRef
    {
        ESM::RefId mRefId{};
        ESM::RefNum mRefNum{};
        osg::Vec3f mPosition{};
        osg::Vec3f mRotation{};
        float mScale = 1.f;

        /// The gate that says whether it stands in the distance, where a script that runs only while
        /// its cell is active enables or disables it — its record's own, or one a reference in its
        /// cell wears that names it — so the content files' answer is not the game's.
        /// `MWScript::VisibilityGates` says which.
        std::uint32_t mGate = sNoGate;
    };

}
