#ifndef OPENMW_COMPONENTS_RTX_SHADERS_WAVE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_WAVE_H

#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// The sea as a set of transformed tiles rather than as a list of sinusoids.
//
// **A sum of plane waves is quasi-periodic, and curvature is where that shows.** The second
// derivative weights a component by `A k²`, so the shortest few own it however the spectrum falls —
// and a handful of plane waves crossing is a lattice, which is what a seabed draws under one. No
// allocation of sixty-four components fixes that: the fix is thousands of components, which is a
// transform.
//
// **And it is the cheaper of the two.** A tile costs three texture fetches where the sum was
// sixty-four sines, and the surface is read twice per water pixel — once for the normal and once for
// the curvature. The synthesis is one pass a frame over a few small grids.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The largest grid any tile is sampled on, along each axis.
    ///
    /// **Large, because the component count is what this is for.** The widest tile holds fifty
    /// thousand wavevectors inside the spectrum's band against the sixty-four the sinusoid table
    /// carried, and that count is the whole difference between a lattice and water. It also sets
    /// how short a wave the tile can hold — two of its texels, sixteen units, against the thirty-two
    /// the spectrum stops at.
    ///
    /// **A ceiling and not the size**, because a narrower tile needs a smaller grid: the same band
    /// of wavelengths occupies fewer of its cells, so `Rtx::sWaveTiles` gives each tile its own and
    /// no transform runs over a quadrant of nothing.
    const uint WAVE_GRID = 512u;

    /// How many tiles the sea is summed from.
    ///
    /// **Two, and the reason is the tile rather than the band.** Morrowind's sea spans a factor of
    /// thirty-two in wavelength where one grid of this size spans five hundred, so splitting the
    /// spectrum between tiles leaves every grid nearly empty. Both tiles carry the *whole* spectrum
    /// instead, at
    /// half its variance each: two independent fields of half the energy sum to one field of the
    /// full energy and the same spectrum, and their periods do not divide into one another, so the
    /// sum repeats only at a common multiple nothing looks across.
    const uint WAVE_CASCADES = 2u;

    /// How many levels the deepest tile's chain has, which is what any table indexed by one is as
    /// long as.
    ///
    /// **The widest grid decides it and the narrow tile is short of it.** A chain runs from the grid
    /// down to one texel, so it holds `log2(WAVE_GRID) + 1` levels; a tile transformed on a smaller
    /// grid has fewer and a sampler clamps to its last. A table over this is then flat past that
    /// tile's own end, which is the answer a clamp gives anyway.
    const uint WAVE_LEVELS = 10u;

    /// How many twiddles the transform reads: `exp(i TAU k / WAVE_GRID)` for `k` below half the
    /// widest grid, which holds every stage's of every grid, since a stage `2 span` long wants
    /// those at multiples of `WAVE_GRID / (2 span)`.
    ///
    /// **A table, because the driver owns the precision of `sin` and `cos`.** Vulkan bounds them to
    /// `2^-11` absolute, and only for an angle inside `[-PI, PI]`, and the error of each of nine
    /// stages carries into the curvature the caustics read. The host computes the table in double and rounds it once.
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

/// A complex number turned by a unit phasor `(cos, sin)`, which is a multiply by it.
///
/// **Shared, because the pass that turns the spectrum and the pass that transforms it both do it.**
/// One is `h0` carried to a time and the other is a butterfly's twiddle, and they are the same two
/// lines — two copies of which are two places for a sign to be wrong.
RTX_SHADER vec2 turnedBy(vec2 value, vec2 phasor)
{
    return vec2(value.x * phasor.x - value.y * phasor.y, value.x * phasor.y + value.y * phasor.x);
}

#endif

#endif
