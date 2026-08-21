#ifndef OPENMW_COMPONENTS_RTX_INSTANCERECORD_H
#define OPENMW_COMPONENTS_RTX_INSTANCERECORD_H

#include <cstdint>
#include <span>
#include <vector>

#include <osg/Matrixf>

#include "scenedesc.hpp"

namespace Rtx
{
    /// An affine transform as three rows of four, translation in the last column.
    ///
    /// The shape an instance descriptor wants and the one OpenSceneGraph does not have.  OSG
    /// multiplies a row vector on the left, so its translation is the last *row*; a descriptor
    /// multiplies a column vector on the right, so the rotation is transposed and the translation
    /// moves to the last column. Getting that wrong mirrors the world about its diagonal, which is
    /// subtle enough on symmetrical architecture to survive being looked at — so the conversion
    /// happens once, here, and a backend only restates these rows in whatever order it stores them.
    struct Transform3x4
    {
        float mRows[3][4];
    };

    Transform3x4 toTransform3x4(const osg::Matrixf& matrix);

    /// One row of the top-level acceleration structure, with every decision already taken.
    ///
    /// **This is where the material policy lives, and it lives here once.** Which rays may see a
    /// surface, and whether traversal has to stop and ask whether a hit is a hole, are answers about
    /// Morrowind's content rather than about an API — and a backend working them out for itself
    /// would be a second place for them to be got wrong.
    struct InstanceRecord
    {
        Transform3x4 mTransform;

        /// The mesh whose bottom-level structure this places.
        Index mMesh = sNoIndex;

        /// Which rays are interested: `Shaders::MASK_SOLID`, or `MASK_WATER` for a surface a shadow
        /// ray must pass straight through. Sunlight reaching a seabed has come through the surface,
        /// so a sea that occluded would black out every shallow in the game — and saying it in the
        /// mask costs traversal nothing, where building the water non-opaque so a candidate loop
        /// could wave shadow rays past was measured at half the frame rate.
        std::uint32_t mMask = 0;

        /// Whether traversal must stop and ask the shader whether a hit is a hole.
        ///
        /// Without it the geometry's own opaque flag stands, traversal commits the first triangle it
        /// meets, and a canopy stays the rectangle it was painted on.
        bool mCutout = false;
    };

    /// Fills `records` with one row per instance the scene places, in that order.
    ///
    /// Two invariants a backend inherits and must not restate differently. A record's index is the
    /// custom index the shader reads back at a hit, so the order here is the order the instance
    /// buffer is built in. And **every instance is built with face culling disabled**: Morrowind
    /// leans heavily on sheet geometry lit and hit from both faces, and a ray tracer has to be told,
    /// because back-face culling is not free for it the way a rasterizer's is.
    ///
    /// An out-parameter refilled with `clear()`, because a cell is thousands of instances and a
    /// rebuild must not go back to the allocator for a buffer it already had.
    void makeInstanceRecords(const SceneDesc& scene, std::vector<InstanceRecord>& records);

    /// How many of `records` traversal has to stop and ask about.
    ///
    /// The cost of the cutout, as a number: every one is a candidate loop and a texture fetch where
    /// an opaque instance is a hit. Reported so a material change that marks half a cell non-opaque
    /// shows up as a number before it shows up as a frame time.
    std::uint32_t countCutouts(std::span<const InstanceRecord> records);
}

#endif
