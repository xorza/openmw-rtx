#ifndef OPENMW_COMPONENTS_RTX_SHADERS_VISIBILITY_H
#define OPENMW_COMPONENTS_RTX_SHADERS_VISIBILITY_H

// Included verbatim by both the shader and the C++ that fills it in, so the two cannot disagree
// about a field. Scalar block layout is what makes that possible: a `vec3` is twelve bytes on both
// sides, with none of the padding rules that make std140 a translation exercise.

#ifdef __cplusplus

#include <cstdint>

#include <osg/Vec3f>
#include <osg/Vec3ui>

namespace Rtx::Shaders
{
    using vec3 = osg::Vec3f;
    using uvec3 = osg::Vec3ui;
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

        /// How far a ray travels before whatever it was looking for counts as not being there.
        ///
        /// The world's own size, near enough: a primary ray that reaches this has left it, and so
        /// has the sun's shadow ray, which is the same question asked from the other end.
        float mFar;

        /// The angle one pixel subtends, in radians.
        ///
        /// A primary ray is the axis of a cone this wide, and the cone's width where it lands is
        /// what decides which mip a texture is read from. Without it every fetch is level zero, the
        /// mip chains are carried and never read, and everything in the distance crawls.
        float mSpreadAngle;

        /// Non-zero to write the albedo straight out, with no shading over it. What a test asserting
        /// "this pixel is that texel" needs, and what makes a texture problem visible as itself.
        uint mShowAlbedo;

        /// Where the sun's light travels, and how much of it arrives on a surface square to it.
        ///
        /// One directional light, handled apart from the point lights because it has no position and
        /// no falloff: it is the same everywhere and its shadow ray runs to the end of the world.
        /// A zero irradiance is how an interior and a night are both said, and the direction must
        /// be a unit vector: it is used as a ray and as a cosine without being normalised again.
        vec3 mSunDirection;
        vec3 mSunIrradiance;

        /// What a ray that hits nothing comes back with, at the horizon and overhead.
        ///
        /// The game's own two colours: its atmosphere is the one overhead and its fog is what that
        /// fades to at the horizon, which is most of what a Morrowind sky is.
        vec3 mSkyHorizon;
        vec3 mSkyZenith;

        /// Where the water's surface is, or negative infinity where the cell holds none.
        ///
        /// Infinity rather than a flag: everything that asks does so as "how deep is this point",
        /// and a level of minus infinity makes that never positive, so a cell with no water takes
        /// the same path as a point above the surface with no branch of its own.
        float mWaterLevel;

        /// How long the water has been moving, in seconds.
        ///
        /// Zero is a still sea and a deterministic frame, which is what a test wants; the window
        /// path passes its own clock.
        float mTime;

        /// The cell's own ambient, linear.
        ///
        /// Morrowind's interiors were authored against a renderer with no bounce at all, so this
        /// term stands in for every one of them — and applying it on top of real light therefore
        /// double-counts, the same way a pre-lit texture does. Both are M9's to unpick.
        vec3 mAmbient;

        /// What the air between the eye and everything else scatters toward it, and how much of it
        /// there is.
        ///
        /// **The colour is the horizon's**, and not by coincidence: Morrowind records one colour for
        /// the fog and the sky's lower half because they are the same thing seen at two distances,
        /// which is why a ray that reaches nothing has to converge on exactly what a ray through a
        /// mile of air does. An interior carries its own in `AMBI` instead.
        ///
        /// The extinction is absolute, per world unit, at the fog's base: the host has already
        /// turned the record's view-range-relative dial into one. Zero is no fog at all and costs
        /// nothing — which is what the tests that measure surface radiance need, since a lit surface
        /// with fog over it is a differently lit one.
        vec3 mFogColour;
        float mFogExtinction;

        /// Where the light grid's cell zero starts, how wide a cell is, and how many there are.
        ///
        /// **The scene's, not the camera's**, and written by the pass rather than by whoever built
        /// the rest of this: the grid belongs to the lamps it was binned from, and a caller setting
        /// these would be repeating what `SceneBuffers` already worked out. A position outside the
        /// grid is one no lamp reaches, so its cell is empty by construction rather than by clamp.
        vec3 mGridOrigin;
        float mGridInverseCell;
        uvec3 mGridSize;
    };

#ifdef __cplusplus
}
#endif

#endif
