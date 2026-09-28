#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>

namespace Rtx
{
    namespace
    {
        /// What one shape pass wrote, kept whole for a test to read.
        struct Shaped
        {
            std::vector<std::uint32_t> mKept;
            std::vector<osg::Vec3f> mNormals;
            std::vector<std::uint32_t> mSources;
            FoldedShape mShape;
        };

        Shaped shape(ContentPreprocessor& content, std::span<const osg::Vec3f> positions,
            std::span<const osg::Vec3f> normals, std::span<const std::uint32_t> triangles, bool splits = true)
        {
            Shaped shaped;
            ShapePass::Output output{ .mKept = shaped.mKept, .mNormals = shaped.mNormals, .mSources = shaped.mSources };
            content.shape(
                ShapePass::Input{
                    .mPositions = positions, .mNormals = normals, .mTriangles = triangles, .mSplits = splits },
                output);
            shaped.mShape = output.mShape;
            return shaped;
        }

        osg::Vec3f faceOf(std::span<const osg::Vec3f> positions, const std::uint32_t* corners)
        {
            osg::Vec3f face
                = (positions[corners[1]] - positions[corners[0]]) ^ (positions[corners[2]] - positions[corners[0]]);
            face.normalize();
            return face;
        }

        /// Whether `a` is the unit vector `b`: a normal the split recomputes is its piece's sum
        /// normalised, which a square root leaves a few ulp off an axis; a millionth is well under
        /// any angle a picture could show.
        ::testing::AssertionResult sameDirection(const osg::Vec3f& a, const osg::Vec3f& b)
        {
            if ((a - b).length() < 1e-6f)
                return ::testing::AssertionSuccess();
            return ::testing::AssertionFailure() << "(" << a.x() << ", " << a.y() << ", " << a.z() << ") is not ("
                                                 << b.x() << ", " << b.y() << ", " << b.z() << ")";
        }

        /// A unit cube, wound outward, as a lazy export spells it: eight vertices, one normal each,
        /// averaged over the three faces meeting there — the diagonal `(±1, ±1, ±1) / √3`.
        struct AveragedCube
        {
            std::array<osg::Vec3f, 8> mPositions;
            std::array<osg::Vec3f, 8> mNormals;
            std::array<std::uint32_t, 36> mTriangles{ 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1,
                6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7 };

            AveragedCube()
            {
                for (std::uint32_t vertex = 0; vertex < 8; ++vertex)
                {
                    const float x = (vertex == 1 || vertex == 2 || vertex == 5 || vertex == 6) ? 1.0f : 0.0f;
                    const float y = (vertex == 2 || vertex == 3 || vertex == 6 || vertex == 7) ? 1.0f : 0.0f;
                    const float z = vertex >= 4 ? 1.0f : 0.0f;
                    mPositions[vertex] = osg::Vec3f(x, y, z);
                    osg::Vec3f normal(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z * 2.0f - 1.0f);
                    normal.normalize();
                    mNormals[vertex] = normal;
                }
            }
        };

        /// **A right angle the content smoothed is cut, and every corner takes its own face's
        /// normal.** Each of a cube's eight corners joins three faces across three right angles, so
        /// each falls into three pieces: the corner keeps its vertex for the first and gains two
        /// copies, 8 + 16 = 24 vertices. A piece is the two coplanar triangles of one face, whose
        /// angle-weighted mean is that face's axis exactly.
        TEST(RtxCreaseSplitTest, aCubeSmoothedAcrossItsRightAnglesIsCutIntoItsFaces)
        {
            ContentPreprocessor content;
            const AveragedCube cube;
            const Shaped shaped = shape(content, cube.mPositions, cube.mNormals, cube.mTriangles);

            ASSERT_EQ(shaped.mNormals.size(), 24u);
            ASSERT_EQ(shaped.mSources.size(), 16u);
            ASSERT_EQ(shaped.mKept.size(), 36u);

            for (std::size_t added = 0; added < shaped.mSources.size(); ++added)
                EXPECT_LT(shaped.mSources[added], 8u) << "an added vertex copies one the cube had";

            for (std::size_t t = 0; t < 12; ++t)
            {
                const osg::Vec3f face = faceOf(cube.mPositions, &cube.mTriangles[3 * t]);
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::uint32_t vertex = shaped.mKept[3 * t + corner];
                    const std::uint32_t source = vertex < 8 ? vertex : shaped.mSources[vertex - 8];
                    EXPECT_EQ(source, cube.mTriangles[3 * t + corner]) << "a corner moved to another point";
                    EXPECT_TRUE(sameDirection(shaped.mNormals[vertex], face))
                        << "triangle " << t << ", corner " << corner;
                }
            }
        }

        /// **A curve the content cut coarsely is left as it was written, and the threshold is what
        /// says how coarse.** A prism of `sides` sides smoothed round meets itself at `360 / sides`
        /// degrees between sides: an octagon's forty-five is under `CreaseSplit::sHardCosine`'s
        /// fifty-five, so nothing is cut and nothing written; a hexagon's sixty is over it, so every
        /// side is cut into its own face and every one of the twelve vertices gains a copy, twelve
        /// added.
        TEST(RtxCreaseSplitTest, anOctagonalPrismStaysRoundAndAHexagonalOneIsCutIntoItsFaces)
        {
            const auto prism = [](int sides, bool cut) {
                std::vector<osg::Vec3f> positions;
                std::vector<osg::Vec3f> normals;
                for (int side = 0; side < sides; ++side)
                {
                    const double angle = side * 2.0 * std::numbers::pi / sides;
                    const osg::Vec3f out(float(std::cos(angle)), float(std::sin(angle)), 0.0f);
                    positions.push_back(out);
                    positions.push_back(out + osg::Vec3f(0.0f, 0.0f, 1.0f));
                    normals.push_back(out);
                    normals.push_back(out);
                }

                std::vector<std::uint32_t> triangles;
                for (std::uint32_t side = 0; side < std::uint32_t(sides); ++side)
                {
                    const std::uint32_t low = 2 * side;
                    const std::uint32_t next = 2 * ((side + 1) % sides);
                    triangles.insert(triangles.end(), { low, next, next + 1, low, next + 1, low + 1 });
                }

                ContentPreprocessor content;
                const Shaped shaped = shape(content, positions, normals, triangles);
                if (!cut)
                {
                    EXPECT_TRUE(shaped.mNormals.empty()) << sides << " sides";
                    EXPECT_EQ(shaped.mKept, triangles) << sides << " sides";
                    return;
                }

                EXPECT_EQ(shaped.mSources.size(), std::size_t(2 * sides)) << sides << " sides";
                for (std::size_t t = 0; t < triangles.size() / 3; ++t)
                {
                    const osg::Vec3f face = faceOf(positions, &triangles[3 * t]);
                    for (std::size_t corner = 0; corner < 3; ++corner)
                        EXPECT_TRUE(sameDirection(shaped.mNormals[shaped.mKept[3 * t + corner]], face))
                            << sides << " sides, triangle " << t;
                }
            };

            prism(8, false);
            prism(6, true);
        }

        /// **What the content already split is not touched**, and neither is anything the pass is
        /// told not to split. The cube with a vertex a face — what an exporter writes for hard
        /// edges — has one normal a corner's group and no group spanning an edge; the averaged cube
        /// asked with no split, as a skinned body is, keeps what it had.
        TEST(RtxCreaseSplitTest, aCubeAlreadySplitAndAMeshThatMayNotSplitAreLeftAsTheyAre)
        {
            const AveragedCube cube;
            std::vector<osg::Vec3f> positions;
            std::vector<osg::Vec3f> normals;
            std::vector<std::uint32_t> triangles;
            for (std::size_t t = 0; t < 12; ++t)
            {
                const osg::Vec3f face = faceOf(cube.mPositions, &cube.mTriangles[3 * t]);
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    triangles.push_back(static_cast<std::uint32_t>(positions.size()));
                    positions.push_back(cube.mPositions[cube.mTriangles[3 * t + corner]]);
                    normals.push_back(face);
                }
            }

            ContentPreprocessor content;
            EXPECT_TRUE(shape(content, positions, normals, triangles).mNormals.empty());

            const Shaped held = shape(content, cube.mPositions, cube.mNormals, cube.mTriangles, false);
            EXPECT_TRUE(held.mNormals.empty());
            EXPECT_TRUE(held.mSources.empty());
        }

        /// **A crease is found across a UV seam, and cut with no vertex added.** Two quads meeting at
        /// a right angle, each with its own copies of the two points they share — as Morrowind
        /// splits a vertex wherever its UVs part — and every copy carrying the one averaged normal
        /// `(0, 1, 1) / √2`. The edge is hard between two vertices that are not the same vertex, so
        /// only the normals change: each quad's copies take its own face's.
        TEST(RtxCreaseSplitTest, aCreaseAcrossAUvSeamChangesTheNormalsAndAddsNoVertex)
        {
            const std::array<osg::Vec3f, 8> positions{
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f), // the floor, facing +z
                osg::Vec3f(0.0f, 1.0f, 0.0f), osg::Vec3f(1.0f, 1.0f, 0.0f), osg::Vec3f(1.0f, 1.0f, 1.0f),
                osg::Vec3f(0.0f, 1.0f, 1.0f), // the wall, facing -y
            };
            const osg::Vec3f averaged = osg::Vec3f(0.0f, -1.0f, 1.0f) / std::sqrt(2.0f);
            const osg::Vec3f floorNormal(0.0f, 0.0f, 1.0f);
            const osg::Vec3f wallNormal(0.0f, -1.0f, 0.0f);
            const std::array<osg::Vec3f, 8> normals{ floorNormal, floorNormal, averaged, averaged, averaged, averaged,
                wallNormal, wallNormal };
            const std::array<std::uint32_t, 12> triangles{ 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };

            ContentPreprocessor content;
            const Shaped shaped = shape(content, positions, normals, triangles);

            ASSERT_EQ(shaped.mNormals.size(), 8u) << "a vertex was added where none needed to be";
            EXPECT_TRUE(shaped.mSources.empty());
            EXPECT_TRUE(sameDirection(shaped.mNormals[2], floorNormal));
            EXPECT_TRUE(sameDirection(shaped.mNormals[3], floorNormal));
            EXPECT_TRUE(sameDirection(shaped.mNormals[4], wallNormal));
            EXPECT_TRUE(sameDirection(shaped.mNormals[5], wallNormal));
            EXPECT_EQ(shaped.mNormals[0], floorNormal) << "a vertex off the crease moved";
            EXPECT_EQ(shaped.mNormals[6], wallNormal);
        }

        /// **A sheet keeps the normals it was written with**, the crease across its cards included.
        /// The same right-angled pair of quads as above, doubled for its back the way the content
        /// doubles a card — every triangle again, wound the other way, on vertices of its own — folds
        /// to a sheet, and a sheet is lit from both faces off its plane, where foliage averages its
        /// cards' normals on purpose.
        TEST(RtxCreaseSplitTest, aSheetKeepsTheNormalsItWasWrittenWith)
        {
            const osg::Vec3f averaged = osg::Vec3f(0.0f, -1.0f, 1.0f) / std::sqrt(2.0f);
            std::vector<osg::Vec3f> positions{
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 1.0f),
                osg::Vec3f(0.0f, 1.0f, 1.0f),
            };
            std::vector<osg::Vec3f> normals(positions.size(), averaged);
            std::vector<std::uint32_t> triangles{ 0, 1, 2, 0, 2, 3, 3, 2, 4, 3, 4, 5 };

            // The back: the same positions again, each triangle reversed on them.
            const std::uint32_t back = static_cast<std::uint32_t>(positions.size());
            for (std::uint32_t vertex = 0; vertex < back; ++vertex)
            {
                positions.push_back(positions[vertex]);
                normals.push_back(-averaged);
            }
            for (std::size_t t = 0; t < 4; ++t)
                triangles.insert(triangles.end(),
                    { back + triangles[3 * t], back + triangles[3 * t + 2], back + triangles[3 * t + 1] });

            ContentPreprocessor content;
            const Shaped shaped = shape(content, positions, normals, triangles);
            EXPECT_TRUE(shaped.mShape.mSheet);
            EXPECT_TRUE(shaped.mNormals.empty()) << "a sheet's normals were cut";

            // The front alone is no sheet, and the same crease is cut.
            triangles.resize(12);
            EXPECT_FALSE(shape(content, positions, normals, triangles).mNormals.empty());
        }

        /// The shape pass is keyed on everything it reads: the positions, the normals and the
        /// triangles, and whether it may split — the cube's eight positions and eight normals of
        /// twelve bytes, thirty-six indices of four, and the flag's one byte: 96 + 96 + 144 + 1.
        TEST(RtxCreaseSplitTest, theShapePassIsKeyedOnTheNormalsAndOnWhetherItSplits)
        {
            ContentPreprocessor content;
            const AveragedCube cube;
            shape(content, cube.mPositions, cube.mNormals, cube.mTriangles);
            EXPECT_EQ(content.takeStats().at(ContentPassId::Shape).mKeyBytes, 96u + 96u + 144u + 1u);
        }
    }
}
