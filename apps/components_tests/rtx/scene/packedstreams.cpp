#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <osg/BoundingBox>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/scene/foldedshape.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/packedstreams.hpp>

namespace Rtx
{
    namespace
    {
        /// **A mesh is packed as the device reads it, bounded and keyed where it is read.** Straight up packs to
        /// `0x9FFFBFFF` (`RtxSceneDescTest`), a quarter to the half `0x3400` and white to `0x3C00`; the box is the
        /// positions'. Two meshes laid end to end each read back their own runs.
        TEST(RtxPackedStreamsTest, aMeshIsPackedBoundedAndKeyedWhereItIsRead)
        {
            const std::array normals{ osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(0.0f, 0.0f, 1.0f) };
            const std::array colours{ osg::Vec3f(0.25f, 1.0f, 1.0f), osg::Vec3f(1.0f, 1.0f, 1.0f),
                osg::Vec3f(1.0f, 1.0f, 1.0f), osg::Vec3f(1.0f, 1.0f, 1.0f) };

            PackedStreams streams;
            const PackedRuns first = streams.append(MeshArrays{ .mPositions = Testing::sUnitQuad,
                                                        .mNormals = normals,
                                                        .mColours = colours,
                                                        .mIndices = Testing::sQuadIndices },
                FoldedShape{ .mSheet = true });
            const PackedRuns second = streams.append(
                MeshArrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices }, FoldedShape{});

            const PackedMesh one = streams.of(first);
            ASSERT_EQ(one.mNormals.size(), 4u);
            EXPECT_EQ(one.mNormals[3], 0x9FFFBFFFu);
            ASSERT_EQ(one.mColours.size(), 4u);
            EXPECT_EQ(one.mColours[0].mRedGreen, 0x3400u | (0x3C00u << 16));
            EXPECT_EQ(one.mColours[0].mBlue, 0x3C00u);
            EXPECT_TRUE(one.mTexCoords.empty()) << "a stream the mesh brought none of";
            EXPECT_TRUE(one.mTangents.empty());
            EXPECT_TRUE(one.mShape.mSheet);
            EXPECT_EQ(one.mBounds, osg::BoundingBoxf(osg::Vec3f(), osg::Vec3f(1.0f, 1.0f, 0.0f)));

            EXPECT_EQ(second.mVertices, (Rtx::Run{ .mOffset = 4, .mCount = 4 }));
            EXPECT_EQ(second.mIndices, (Rtx::Run{ .mOffset = 6, .mCount = 6 }));
            EXPECT_TRUE(second.mNormals.empty());
            EXPECT_EQ(streams.of(second).mPositions[2], Testing::sUnitQuad[2]);

            // Each fact of the mesh moves its key: its shape, and its normals and colours, which the second lacks.
            EXPECT_NE(first.mKey, second.mKey);
            PackedStreams again;
            EXPECT_EQ(again
                          .append(MeshArrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices },
                              FoldedShape{})
                          .mKey,
                second.mKey)
                << "one mesh read twice is two keys";
            EXPECT_NE(again
                          .append(MeshArrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices },
                              FoldedShape{ .mSheet = true })
                          .mKey,
                second.mKey)
                << "the shape is not in the key";
        }

        /// **Tangents that all pack to nought are none**, a generator's of no length: the mesh carries an empty
        /// stream, and keys as one that brought none. One that is not nought keeps them all.
        TEST(RtxPackedStreamsTest, tangentsThatAllPackToNoughtAreNone)
        {
            const std::array<osg::Vec4f, 4> none{};
            PackedStreams streams;
            const PackedRuns flat = streams.append(
                MeshArrays{ .mPositions = Testing::sUnitQuad, .mTangents = none, .mIndices = Testing::sQuadIndices },
                FoldedShape{});
            const PackedRuns bare = streams.append(
                MeshArrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices }, FoldedShape{});
            EXPECT_TRUE(flat.mTangents.empty());
            EXPECT_EQ(flat.mKey, bare.mKey);

            std::array<osg::Vec4f, 4> one{};
            one[2] = osg::Vec4f(0.0f, 1.0f, 0.0f, 1.0f);
            const PackedRuns lit = streams.append(
                MeshArrays{ .mPositions = Testing::sUnitQuad, .mTangents = one, .mIndices = Testing::sQuadIndices },
                FoldedShape{});
            ASSERT_EQ(lit.mTangents.mCount, 4u);
            EXPECT_EQ(lit.mTangents.mOffset, 0u) << "the tangents dropped left their room behind";
            EXPECT_EQ(streams.of(lit).mTangents[0], 0u);
            EXPECT_EQ(streams.of(lit).mTangents[2], 0xBFFF3FFFu) << "along y, `RtxSceneDescTest`'s word";
        }

        /// **A reuse keeps the room its content filled at least half of**, as a spare model keeps its buffers
        /// (`dropSlack`): the two quads above hold 8 positions of 12 bytes, and a clear keeps them.
        TEST(RtxPackedStreamsTest, aReuseKeepsTheRoomItsContentFilled)
        {
            PackedStreams streams;
            streams.append(MeshArrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices }, {});
            const std::size_t room = streams.getRoomBytes();
            EXPECT_EQ(streams.getContentBytes(), 4 * sizeof(osg::Vec3f) + 6 * sizeof(std::uint32_t));
            ASSERT_GE(room, streams.getContentBytes());

            streams.clear();
            EXPECT_EQ(streams.getContentBytes(), 0u);
            EXPECT_EQ(streams.getRoomBytes(), room) << "a clear gave the room back";

            streams.reuse();
            EXPECT_EQ(streams.getRoomBytes(), 0u) << "a reuse of nothing kept the room";

            streams.append(MeshArrays{ .mPositions = Testing::sUnitQuad, .mIndices = Testing::sQuadIndices }, {});
            streams.letGo();
            EXPECT_EQ(streams.getRoomBytes(), 0u);
        }
    }
}
