#pragma once

#include <cstddef>
#include <cstdint>

#include "levelasks.hpp"

namespace Rtx
{
    /// How many levels a texture's asks name: a 16384-square chain's fifteen, and one past them, where every chain
    /// stands its last level.
    inline constexpr std::size_t sTextureLevels = 16;

    /// The levels a texture is asked at, by the materials worn where it is read and by what reads it itself
    /// (`TextureTable::getWant`).
    using TextureAsks = LevelAsks<sTextureLevels>;

    /// **A material's footprint, in whole steps of the level a texture of one texel per coordinate unit would be read
    /// at**: half the log of its placement's texture-to-world area ratio (`UvDensity`), less the log of its scale, plus
    /// the log of its cone's width and the frame's level bias. A texture of `w` by `h` read through a transform that
    /// lays `T` of its coordinates on each of the mesh's is read at that step plus `0.5 log2(T w h)` (`levelAsked`).
    /// The scale runs from `sFootprintFinest` up, the finest step standing for every footprint at or under it, so a
    /// footprint is rounded to the finer step at each end: the level asked is never coarser than the one read.
    inline constexpr std::int32_t sFootprintFinest = -32;
    inline constexpr std::size_t sFootprintSteps = 48;

    /// The footprints a material is asked at: by the ring's cells, each at its own distance, and by every placement the
    /// walk stands wearing it, at the finest step.
    using FootprintAsks = LevelAsks<sFootprintSteps>;

    /// The step `footprint` falls on, rounded down and held to the scale: a footprint of minus infinity, a cone of no
    /// width, the finest; one not a number, the finest as well.
    std::size_t footprintStep(float footprint);

    /// The level a footprint `step` asks of a texture read at `offset`, `0.5 log2(T w h)`, held to `ceiling`: rounded
    /// down, and nought for an offset of minus infinity, which a texture read by direction or of no known size states.
    std::size_t levelAsked(std::size_t step, float offset, std::size_t ceiling = sTextureLevels - 1);
}
