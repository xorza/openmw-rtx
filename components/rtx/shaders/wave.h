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

    /// Where `waverows.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint WAVE_ROWS_BIND_AMPLITUDES = 0;
    const uint WAVE_ROWS_BIND_TURN_RATES = 1;
    const uint WAVE_ROWS_BIND_FIELD = 2;
    const uint WAVE_ROWS_BINDINGS = 3;

    /// Where `wavecolumns.comp` binds what it reads and writes in set 0, and how many there are, by
    /// the same rule.
    const uint WAVE_COLUMNS_BIND_FIELD = 0;
    const uint WAVE_COLUMNS_BIND_SURFACE = 1;
    const uint WAVE_COLUMNS_BIND_CURVATURE = 2;
    const uint WAVE_COLUMNS_BINDINGS = 3;

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

#ifdef RTX_HOST
    static_assert((1u << (WAVE_LEVELS - 1u)) == WAVE_GRID, "WAVE_LEVELS must be the widest grid's own chain");
#endif

    /// Threads in a transform workgroup, one per butterfly.
    ///
    /// A radix-2 pass over `n` points is `n / 2` butterflies, so the largest grid wants half its own
    /// width. Vulkan promises a thousand and twenty-four threads to a workgroup and thirty-two
    /// kibibytes of shared memory, against the twelve its three lines hold.
    const uint WAVE_WORKGROUP = WAVE_GRID / 2u;

    /// What the row pass, which forms the spectra and transforms them along the rows, is told.
    ///
    /// **One thread a wavevector, and it forms all three pairs.** Every pair is the same `H(k, t)`
    /// times a different power of `ik`, so forming them together reads the amplitude once where
    /// three would read it three times.
    struct WaveRowsConstants
    {
        /// Points along each axis of this tile's grid.
        uint mCount;

        /// How wide the tile is in world units, which turns a grid index into a wavevector.
        float mExtent;

        /// How far the sea has run, in seconds, as two floats whose sum is the host's double —
        /// `Rtx::splitSeconds`, which `turnsAt` reduces exactly. The whole of what a frame changes.
        vec2 mTime;
    };

    /// What the column pass, which transforms the fields along the columns and unpacks them, is told.
    struct WaveColumnsConstants
    {
        /// Points along each axis of this tile's grid.
        uint mCount;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(WaveRowsConstants) == 16, "WaveRowsConstants must be scalar-packed on every side");
    static_assert(sizeof(WaveColumnsConstants) == 4, "WaveColumnsConstants must be scalar-packed on every side");
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

/// A complex number turned by an angle, which is a multiply by `exp(i angle)`.
///
/// **Shared, because the pass that turns the spectrum and the pass that transforms it both do it.**
/// One is `h0` carried to a time and the other is a butterfly's twiddle, and they are the same four
/// lines — two copies of which are two places for a sign to be wrong.
RTX_SHADER vec2 turnedBy(vec2 value, float angle)
{
    const float sine = sin(angle);
    const float cosine = cos(angle);

    return vec2(value.x * cosine - value.y * sine, value.x * sine + value.y * cosine);
}

#endif

#endif
