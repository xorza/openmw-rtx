// `#pragma once` everywhere else in this tree, and an include guard here: `glslc` warns
// "'#pragma once' : not implemented" and carries on, so a header included twice by one
// shader would redefine everything in it.
#ifndef OPENMW_COMPONENTS_RTX_SHADERS_ATROUS_H
#define OPENMW_COMPONENTS_RTX_SHADERS_ATROUS_H

#include "portable.h"

// What one wavelet level of the denoiser needs. Included verbatim by both sides, for the reason
// `visibility.h` is.

#ifdef RTX_HOST

#include <cstdint>

#include <osg/Vec2f>
#include <osg/Vec3f>

namespace Rtx::Shaders
{
    using vec2 = osg::Vec2f;
    using vec3 = osg::Vec3f;
    using uint = std::uint32_t;

#endif

    /// Threads along each edge of a level's workgroup.
    RTX_CONST uint ATROUS_WORKGROUP = 8;

    /// How far apart a level's taps stand, doubling each level: 1, 2, 4, 8, 16.
    ///
    /// **Five levels of a 5×5 kernel reach sixty-two pixels.** Each takes two taps at its own
    /// spacing, so the cascade's support is twice `1 + 2 + 4 + 8 + 16`. That is the à-trous trick —
    /// the holes between taps grow while the tap count does not, so a hundred and twenty-five
    /// samples do what a single kernel of that reach would need fifteen thousand for.
    RTX_CONST uint ATROUS_LEVELS = 5;

    /// Everything one level reads that is not an image.
    ///
    /// **The camera is here because the edge tests need world positions and the guide stores a
    /// distance.** A position is `origin + direction * distance`, and the difference between two of
    /// them drops the origin — so the basis is enough and the eye's place in the world is not
    /// needed. The rays are rebuilt exactly as the trace built them, which is what makes the
    /// reconstructed positions the ones that were actually shaded — and why every field the trace
    /// used to build one is carried here, not merely the ones a pinhole needs.
    struct AtrousConstants
    {
        vec3 mForward;
        vec3 mRight;
        vec3 mUp;

        /// Non-zero for a parallel projection, meaning exactly what
        /// `VisibilityConstants::mOrthographic` means: `mRight` and `mUp` are the half-extents of a
        /// box in world units rather than of the image plane, and a pixel's offset moves where its
        /// ray *starts* instead of where it points.
        ///
        /// **A local map tile is one**, and it reaches this filter like any other traced view.
        uint mOrthographic;

        uint mWidth;
        uint mHeight;

        /// Where inside the pixel the trace sampled, which has to be added back or the positions
        /// reconstructed here are half a pixel from the ones that were shaded.
        vec2 mJitter;

        /// The angle one pixel subtends, which turns a distance into a footprint.
        ///
        /// **Nought under a parallel projection, and that is not a missing answer.** A parallel
        /// ray's cone never widens; its footprint is one pixel of the box for the whole of its
        /// length, and `mRight` is what carries half of that.
        float mSpreadAngle;

        /// The spacing of this level's taps, in pixels.
        uint mStep;

        /// How sharply the normals have to agree, as the exponent on their cosine.
        ///
        /// A hundred and twenty-eight keeps a tap at more than about six degrees of tilt from
        /// contributing anything, which is what stops a wall bleeding into the floor it meets.
        float mNormalPower;

        /// How far off the centre pixel's tangent plane a tap may sit, in pixel footprints.
        ///
        /// **Off the plane, not away from the eye.** Terrain seen at a grazing angle steps a long
        /// way in distance between neighbouring pixels while remaining one flat surface, so a test
        /// on distance alone would refuse to filter exactly the ground that most needs it.
        float mPlaneSigma;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#if defined(RTX_HOST) || defined(__METAL_VERSION__)
    static_assert(sizeof(AtrousConstants) == 72, "AtrousConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
