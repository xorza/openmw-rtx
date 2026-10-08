#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_WAVETRANSFORM_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_WAVETRANSFORM_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/wave.h>

// The sea's transform on the device, `waverows.comp` and `wavecolumns.comp`: where each binds what it
// reads and writes, how wide its workgroups are, and what each dispatch is told. `wave.h` says what
// a tile is.

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

#ifdef RTX_HOST
    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
    static_assert(sizeof(WaveRowsConstants) == 16, "WaveRowsConstants must be scalar-packed on every side");
    static_assert(sizeof(WaveColumnsConstants) == 4, "WaveColumnsConstants must be scalar-packed on every side");
}
#endif

#endif
