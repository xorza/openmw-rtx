#ifndef OPENMW_COMPONENTS_RTX_SHADERS_WAVE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_WAVE_H

#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// The sea as a set of transformed tiles rather than as a list of sinusoids. **A sum of plane waves is
// quasi-periodic, and curvature is where that shows**: the second derivative weights a component by
// `A k²`, so the shortest few own it, and a handful of plane waves crossing is a lattice, which a
// seabed draws under one. No allocation of sixty-four components fixes that; thousands do, which is a
// transform. **It is also the cheaper**: a tile costs three fetches where the sum was sixty-four sines,
// read twice per water pixel (normal and curvature), with one synthesis pass a frame over a few grids.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The largest grid any tile is sampled on, along each axis. **Large, because the component count
    /// is what this is for**: the widest tile holds fifty thousand wavevectors in the spectrum's band
    /// against the sixty-four the sinusoid table carried, and it sets how short a wave a tile can hold
    /// (two texels, sixteen units, against the thirty-two the spectrum stops at). **A ceiling and not
    /// the size**: a narrower tile needs a smaller grid, which `Rtx::sWaveTiles` gives each tile so no
    /// transform runs over a quadrant of nothing.
    const uint WAVE_GRID = 512u;

    /// How many tiles the sea is summed from. **Two, and the reason is the tile rather than the
    /// band**: Morrowind's sea spans a factor of thirty-two in wavelength where one grid of this size
    /// spans five hundred, so splitting the spectrum leaves every grid nearly empty. Both tiles carry
    /// the *whole* spectrum at half its variance each: independent fields sum to the full energy, and
    /// their periods do not divide into one another, so the sum repeats only at a common multiple
    /// nothing looks across.
    const uint WAVE_CASCADES = 2u;

    /// How many levels the deepest tile's chain has, which is what any table indexed by one is as
    /// long as. **The widest grid decides it**: a chain runs from the grid down to one texel, so it
    /// holds `log2(WAVE_GRID) + 1` levels, and a narrower tile's sampler clamps to its last, which is
    /// the answer a table flat past that tile's own end gives anyway.
    const uint WAVE_LEVELS = 10u;

    /// How many twiddles the transform reads: `exp(i TAU k / WAVE_GRID)` for `k` below half the widest
    /// grid, which holds every stage's of every grid (a stage `2 span` long wants multiples of
    /// `WAVE_GRID / (2 span)`). **A table, because the driver owns the precision of `sin` and `cos`**:
    /// Vulkan bounds them to `2^-11` absolute, only inside `[-PI, PI]`, and the error of each of nine
    /// stages carries into the curvature the caustics read. The host computes it in double, rounded once.
    const uint WAVE_TWIDDLES = WAVE_GRID / 2u;

#ifdef RTX_HOST
    static_assert((1u << (WAVE_LEVELS - 1u)) == WAVE_GRID, "WAVE_LEVELS must be the widest grid's own chain");
#endif

#ifdef RTX_HOST
}
#endif

// What a tile is made of. Half floats: a slope is a fraction and a curvature a small number, and
// both are read at every water pixel through a mip chain the sampler filters. The elevation squared
// in the last channel is the sea's own variance and stays within a half's range at any sea state
// the weather asks for.
#define WAVE_TILE_FORMAT STORAGE_RGBA16F

// What both shading languages read and nothing on this side calls.
#ifndef RTX_HOST

/// A complex number turned by a unit phasor `(cos, sin)`, which is a multiply by it. **Shared, because
/// the pass that turns the spectrum and the pass that transforms it both do it**: `h0` carried to a
/// time and a butterfly's twiddle are the same two lines.
RTX_SHADER vec2 turnedBy(vec2 value, vec2 phasor)
{
    return vec2(value.x * phasor.x - value.y * phasor.y, value.x * phasor.y + value.y * phasor.x);
}

#endif

#endif
