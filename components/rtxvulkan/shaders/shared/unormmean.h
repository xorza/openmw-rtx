#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_UNORMMEAN_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_UNORMMEAN_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the unorm-mean probe is handed: a running mean kept as a shadow field's is
// (`lib/shadowword.glsl`), blended toward a target once a dispatch, as a history is blended once a
// frame, and rounded at random or to the nearest step. Included verbatim by both sides, for the
// reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint UNORM_MEAN_WORKGROUP = 64;

    /// The running means, one word a texel along the image's first row.
    const uint UNORM_MEAN_BIND_HISTORY = 0;

    struct UnormMeanConstants
    {
        /// What each mean is blended toward, and how much of the mean each blend keeps.
        float mTarget;
        float mKept;

        /// Non-zero where the blend is rounded at random, and to the nearest step where nought.
        uint mRounded;

        /// The dispatch's number, which the draws are seeded with as a frame's are.
        uint mFrame;

        /// How many means there are.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(UnormMeanConstants) == 20, "UnormMeanConstants must be scalar-packed on every side");
}
#endif

#endif
