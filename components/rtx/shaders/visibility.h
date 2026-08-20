#ifndef OPENMW_COMPONENTS_RTX_SHADERS_VISIBILITY_H
#define OPENMW_COMPONENTS_RTX_SHADERS_VISIBILITY_H

// Included verbatim by both the shader and the C++ that fills it in, so the two cannot disagree
// about a field. Scalar block layout is what makes that possible: a `vec3` is twelve bytes on both
// sides, with none of the padding rules that make std140 a translation exercise.

#ifdef __cplusplus

#include <cstdint>

#include <osg/Vec3f>

namespace Rtx::Shaders
{
    using vec3 = osg::Vec3f;
    using uint = std::uint32_t;

#endif

    /// Threads along each edge of a workgroup.
    ///
    /// The shader declares its local size from this and the dispatch rounds the image up to it, so
    /// the two cannot drift: writing the number twice is how a pass quietly stops covering its last
    /// row of pixels.
    const uint VISIBILITY_WORKGROUP = 8;

    /// The camera, as a ray generator.
    ///
    /// `mRight` and `mUp` are already scaled by the half-extents of the image plane at unit distance,
    /// so a ray is `mForward + mRight * x + mUp * y` for `x` and `y` in [-1, 1] and no trigonometry
    /// in the shader.
    struct VisibilityConstants
    {
        vec3 mOrigin;
        vec3 mForward;
        vec3 mRight;
        vec3 mUp;

        uint mWidth;
        uint mHeight;

        /// How far a primary ray travels before it counts as having hit the sky.
        float mFar;

        /// Non-zero to write the albedo straight out, with no shading over it. What a test asserting
        /// "this pixel is that texel" needs, and what makes a texture problem visible as itself.
        uint mShowAlbedo;
    };

#ifdef __cplusplus
}
#endif

#endif
