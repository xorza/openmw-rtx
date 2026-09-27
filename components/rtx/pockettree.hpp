#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Vec3f>

namespace Rtx
{
    /// A tree over a shape's triangles, for the two questions `ShapeFold` asks of a pocket: what a
    /// short ray leaving a triangle meets first among the triangles turned to face it, through the
    /// boxes, and the generalized winding number at a point, through the dipole each node stands
    /// for when it is far enough off (Barill, Dickson, Schmidt, Levin and Jacobson, "Fast Winding
    /// Numbers for Soups and Clouds", 2018). Built again for every shape, into buffers kept between
    /// them.
    class PocketTree
    {
    public:
        /// What a ray met first: how far, infinity where nothing, and which triangle.
        struct Facing
        {
            float mDistance;
            std::uint32_t mTriangle;
        };

        /// Builds over the triangles `indices` names, leaving out any with no area. Both spans are
        /// read again by every query, so they have to outlive the queries made before the next
        /// build.
        void build(std::span<const osg::Vec3f> positions, std::span<const std::uint32_t> indices);

        /// The box every triangle with an area stands in, invalid where none has one.
        const osg::BoundingBoxf& getBounds() const;

        /// Triangle `t`'s unit normal, or nought where it has no area.
        const osg::Vec3f& getNormal(std::uint32_t t) const { return mNormals[t]; }

        /// What the ray from `from` along triangle `t`'s own normal meets first, further than
        /// `nearest` and no further than `reach`, of the triangles whose normals lie within `facing`
        /// of its reverse — the cosine between the two normals under which a triangle faces it.
        Facing firstFacing(std::uint32_t t, const osg::Vec3f& from, float nearest, float reach, float facing);

        /// The generalized winding number of the whole shape at `point`: exact near it, and each
        /// far node's dipole beyond.
        double windingAt(const osg::Vec3f& point);

    private:
        struct Node
        {
            osg::BoundingBoxf mBox;

            /// The node's triangles' centroid, weighted by area, and the farthest corner from it.
            osg::Vec3f mCentre;
            float mRadius = 0.0f;

            /// The sum of the node's triangles' area vectors — each its normal times its area —
            /// which is the whole of the node seen from far off.
            osg::Vec3f mDipole;

            /// A leaf's first triangle in `mOrder` and how many, or an inner node's first child,
            /// whose sibling follows it, and nought.
            std::uint32_t mFirst = 0;
            std::uint32_t mCount = 0;
        };

        const osg::Vec3f& corner(std::uint32_t t, int which) const { return mPositions[mIndices[3 * t + which]]; }

        std::span<const osg::Vec3f> mPositions;
        std::span<const std::uint32_t> mIndices;

        /// Per triangle: its unit normal, or nought for one with no area, its area and its centroid.
        std::vector<osg::Vec3f> mNormals;
        std::vector<float> mAreas;
        std::vector<osg::Vec3f> mCentroids;

        std::vector<Node> mNodes;

        /// The triangles with an area, in the leaves' order.
        std::vector<std::uint32_t> mOrder;

        /// What the build and both queries walk the tree with.
        std::vector<std::uint32_t> mStack;
    };
}
