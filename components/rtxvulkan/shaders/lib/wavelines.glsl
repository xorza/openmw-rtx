#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WAVELINES_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WAVELINES_GLSL

// One line of each of the three packed fields, transformed together in shared memory: what
// `waverows.comp` and `wavecolumns.comp` both run, a workgroup a line.
//
// **Cooley-Tukey with the permutation done on the way in.** Stockham avoids the bit reversal by
// carrying an extra buffer through every stage; reversing an index is one instruction here, and the
// load is going to shared memory anyway. Nine stages at the largest grid, each a barrier apart.
//
// **Three lines a workgroup and not one**, because every field of a row or a column is formed from,
// or composed into, the same cells: the row pass forms the three from one amplitude, and the column
// pass writes a texel out of all three. A line a workgroup would put each field through memory
// between the passes that read the others.

#include "scene.h"
#include "shared/sets.h"
#include "shared/wavetransform.h"
#include "wave.h"

/// `exp(i TAU k / WAVE_GRID)` for each `k` below `WAVE_TWIDDLES`, from the host.
layout(set = SET_PASS, binding = WAVE_BIND_TWIDDLES, scalar) readonly buffer Twiddles
{
    vec2 twiddles[WAVE_TWIDDLES];
};

/// The three lines a workgroup transforms, before and after. Ping-pong is what Stockham buys and
/// this does not: one array a field, and a barrier between reading it and writing it.
shared vec2 gLines[3][WAVE_GRID];

/// Where the point `at` of a line `2^stages` long is put on the way in: its index with the low
/// `stages` bits reversed, which is what puts a decimation-in-time transform in order coming out.
/// `bitfieldReverse` turns the whole word, so the slot is the top `stages` bits of it.
uint reversedSlot(uint at, uint stages)
{
    return bitfieldReverse(at) >> (32u - stages);
}

/// Transforms the three lines of `count` points in place, after a barrier that ends the load.
///
/// **The whole workgroup reaches every barrier**, which is why the idle threads of a narrow tile
/// are turned away inside the loop rather than before it: the largest grid sizes the workgroup and
/// a smaller one leaves most of it with nothing to do.
void transformLines(uint count)
{
    const uint thread = gl_LocalInvocationID.x;
    const uint stages = findMSB(count);
    const bool works = thread < count / 2u;

    for (uint stage = 0u; stage < stages; ++stage)
    {
        if (works)
        {
            const uint span = 1u << stage;

            // Each thread owns one butterfly a line: the pair `span` apart inside a block of
            // `2 span`. The map is a partition, so no thread reads what another writes and one
            // barrier a stage is the whole of the ordering.
            const uint within = thread & (span - 1u);
            const uint lower = ((thread - within) << 1u) + within;

            // **Positive, which is what makes this the inverse.** The forward transform turns the
            // other way, and the sea's amplitudes are stated as what an inverse turns into a
            // surface. `within / (2 span)` of a turn, and one twiddle for the three lines.
            const vec2 twiddle = twiddles[within * (WAVE_GRID / (span << 1u))];
            for (int field = 0; field < 3; ++field)
            {
                const vec2 rotated = turnedBy(gLines[field][lower + span], twiddle);
                const vec2 held = gLines[field][lower];

                gLines[field][lower] = held + rotated;
                gLines[field][lower + span] = held - rotated;
            }
        }

        barrier();
    }
}

#endif
