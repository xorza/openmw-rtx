#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SPECULAR_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SPECULAR_H

#include "camera.h"
#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// What the glossy filter needs: a temporal accumulator over `CHANNEL_SPECULAR`, which
// `specular.comp` says the shape of. Included verbatim by both sides, for the reason `visibility.h`
// is.
//
// **The mean in full floats, and its frame count in the fourth channel.** Halves round toward
// nought where this device stores them, so a running mean kept in halves falls a little at every
// store: measured on a metal floor, sixteen frames stood 0.13 to 0.2% under the average of the same
// frames, where full floats keep the average to its rounding. One texel, so a tap is one fetch.

#define SPECULAR_MEAN STORAGE_RGBA32F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `specular.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SPECULAR_BIND_SPECULAR = 0;
    const uint SPECULAR_BIND_SURFACE = 1;
    const uint SPECULAR_BIND_MOTION = 2;
    const uint SPECULAR_BIND_PUFFS = 3;
    const uint SPECULAR_BIND_HELD_SURFACE = 4;
    const uint SPECULAR_BIND_MEAN_BEFORE = 5;
    const uint SPECULAR_BIND_MEAN = 6;
    const uint SPECULAR_BINDINGS = 7;

    /// Threads along each edge of the glossy filter's workgroup.
    const uint SPECULAR_WORKGROUP = 8;

    /// What the glossy filter reads that is not an image.
    struct SpecularConstants
    {
        /// The two eyes a pixel's ray can have left: the direction it saw its surface from, and the
        /// jitter its motion vector was written against.
        Camera mCamera;
        Camera mArms;

        /// The previous frame's basis, `VisibilityConstants::mPreviousForward` and its two
        /// neighbours, and how much wider the arms' plane is — `mArmsSpread`: what the direction a
        /// surface was seen from on the previous frame is read off. All nought where there was no
        /// previous frame.
        vec3 mPreviousForward;
        vec3 mPreviousRight;
        vec3 mPreviousUp;
        vec2 mArmsSpread;

        /// Non-zero where there is no history to reuse, as the accumulator is told.
        uint mReset;

        /// What the accumulator's surface history scaled a distance by —
        /// `AccumulateConstants::mDistanceScale`, the same number, because it is the same history.
        float mDistanceScale;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(SpecularConstants) == 172, "SpecularConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
