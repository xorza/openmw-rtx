#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HALFMEAN_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HALFMEAN_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the half-mean probe is handed: a running mean kept in a half-float image, blended toward a
// target once a dispatch, as a history is blended once a frame, and stored either as it is or rounded
// at random first (`roundedToHalf`). Included verbatim by both sides, for the reason `visibility.h`
// is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint HALF_MEAN_WORKGROUP = 64;

    /// The running means, one a texel along the image's first row.
    const uint HALF_MEAN_BIND_HISTORY = 0;

    struct HalfMeanConstants
    {
        /// What each mean is blended toward, and how much of the mean each blend keeps.
        float mTarget;
        float mKept;

        /// Non-zero where the blend is rounded at random before its store.
        uint mRounded;

        /// The dispatch's number, which the draws are seeded with as a frame's are.
        uint mFrame;

        /// How many means there are.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(HalfMeanConstants) == 20, "HalfMeanConstants must be scalar-packed on every side");
}
#endif

#endif
