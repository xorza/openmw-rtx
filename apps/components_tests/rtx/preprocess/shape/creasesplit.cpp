#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Quat>
#include <osg/Vec3f>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/shape/creasesplit.hpp>
#include <components/rtx/preprocess/shape/facecross.hpp>
#include <components/rtx/preprocess/shape/probetable.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/shaders/tangent.h>
#include <components/testing/util.hpp>

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
            std::span<const osg::Vec3f> normals, std::span<const std::uint32_t> triangles, bool splits = true,
            const CreaseSplit::Beside& beside = {})
        {
            Shaped shaped;
            ShapePass::Output output{ .mKept = shaped.mKept, .mNormals = shaped.mNormals, .mSources = shaped.mSources };
            content.shape(ShapePass::Input{ .mPositions = positions,
                              .mNormals = normals,
                              .mTriangles = triangles,
                              .mSplits = splits,
                              .mBeside = beside },
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

        /// Whether `a` is the unit vector `b` as the pass hands a normal over, its word read back
        /// (`ShapePass::run`): a normal the split recomputes is its piece's sum normalised, which a square root
        /// leaves a few ulp off an axis; a millionth is well under any angle a picture could show.
        ::testing::AssertionResult sameDirection(const osg::Vec3f& a, const osg::Vec3f& b)
        {
            if ((a - Shaders::unpackNormal(Shaders::packNormal(b))).length() < 1e-6f)
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

        /// **A shape is one output with a store or without**: its normals are their words read back, the form a store
        /// keeps them in, so a pass run with no store, one run into a store and one found there hand over the same
        /// floats. The cube is turned off its axes, whose face normals a word would hold exactly.
        TEST(RtxCreaseSplitTest, aShapeIsOneOutputWithAStoreOrWithout)
        {
            AveragedCube cube;
            const osg::Quat turn(0.7, osg::Vec3d(1.0, 2.0, 3.0) / std::sqrt(14.0));
            for (osg::Vec3f& position : cube.mPositions)
                position = turn * position;
            for (osg::Vec3f& normal : cube.mNormals)
                normal = turn * normal;

            ContentPreprocessor alone;
            const Shaped plain = shape(alone, cube.mPositions, cube.mNormals, cube.mTriangles);
            ASSERT_EQ(plain.mNormals.size(), 24u);
            for (const osg::Vec3f& normal : plain.mNormals)
                EXPECT_EQ(Shaders::unpackNormal(Shaders::packNormal(normal)), normal) << "a normal no word holds";

            ContentStore::Store store(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            ContentPreprocessor stored(&store, ContentStore::Reach::Disk);
            const Shaped run = shape(stored, cube.mPositions, cube.mNormals, cube.mTriangles);
            store.flush();
            const Shaped found = shape(stored, cube.mPositions, cube.mNormals, cube.mTriangles);
            EXPECT_EQ(stored.takeStats().at(ContentPassId::Shape).mHits, 1u) << "the second was found in the store";
            for (const Shaped* other : { &run, &found })
            {
                EXPECT_EQ(other->mNormals, plain.mNormals);
                EXPECT_EQ(other->mKept, plain.mKept);
                EXPECT_EQ(other->mSources, plain.mSources);
            }
        }

        /// **A curve the content cut coarsely stays round, and the threshold is what says how coarse.** A prism of
        /// `sides` sides smoothed round meets itself at `360 / sides` degrees: an octagon's forty-five is under
        /// `CreaseSplit::sHardCosine`'s fifty-five, so nothing is cut and every normal stays the radial one it was
        /// written with (each corner sees two equal sides at a quarter turn each, the weighted mean their bisector);
        /// a hexagon's sixty is over it, so every side is cut into its own face and each of the twelve vertices gains
        /// a copy.
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
                    EXPECT_TRUE(shaped.mSources.empty()) << sides << " sides";
                    EXPECT_EQ(shaped.mKept, triangles) << sides << " sides";
                    ASSERT_EQ(shaped.mNormals.size(), normals.size()) << sides << " sides";
                    for (std::size_t vertex = 0; vertex < normals.size(); ++vertex)
                        EXPECT_TRUE(sameDirection(shaped.mNormals[vertex], normals[vertex]))
                            << sides << " sides, vertex " << vertex;
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

        /// **A broad face smoothed into a narrow bevel stays nearly flat, and a flat group keeps
        /// what it was written with.** A floor of 100 by 100 on z = 0 and a 45° bevel from its
        /// y = 0 edge down to y = z = -10, smoothed into one normal along that edge, as a chamfered
        /// Morrowind box is. A triangle weighs its twice-area times its corner angle, and at either
        /// corner of the edge the floor's triangles turn a quarter between them, as the bevel's do:
        /// the floor weighs `10000 * pi / 2` and the bevel `1000 * sqrt(2) * pi / 2`. Summed with
        /// the quarter dropped, `10000 * (0, 0, 1) + 1000 * (0, -1, 1) = (0, -1000, 11000)`: the
        /// direction `(0, -1, 11) / sqrt(122)`, 5.2° off the floor where the content leaned it 45°.
        /// The bevel's lower edge is the bevel alone, one plane, so the normal written there stays,
        /// bent as it is.
        TEST(RtxCreaseSplitTest, aBroadFaceSmoothedIntoANarrowBevelStaysNearlyFlat)
        {
            const osg::Vec3f leaning = osg::Vec3f(0.0f, -1.0f, 1.0f) / std::sqrt(2.0f);
            const osg::Vec3f up(0.0f, 0.0f, 1.0f);
            const osg::Vec3f bent = osg::Vec3f(0.0f, -1.0f, 0.2f) / std::sqrt(1.04f);
            const std::array<osg::Vec3f, 6> positions{ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(100.0f, 0.0f, 0.0f),
                osg::Vec3f(100.0f, 100.0f, 0.0f), osg::Vec3f(0.0f, 100.0f, 0.0f), osg::Vec3f(100.0f, -10.0f, -10.0f),
                osg::Vec3f(0.0f, -10.0f, -10.0f) };
            const std::array<osg::Vec3f, 6> normals{ leaning, leaning, up, up, bent, bent };
            const std::array<std::uint32_t, 12> triangles{ 0, 1, 2, 0, 2, 3, 0, 4, 1, 0, 5, 4 };

            ContentPreprocessor content;
            const Shaped shaped = shape(content, positions, normals, triangles);

            ASSERT_EQ(shaped.mNormals.size(), 6u);
            EXPECT_TRUE(shaped.mSources.empty()) << "a smooth bend cuts nothing";
            const osg::Vec3f weighed = osg::Vec3f(0.0f, -1.0f, 11.0f) / std::sqrt(122.0f);
            EXPECT_TRUE(sameDirection(shaped.mNormals[0], weighed));
            EXPECT_TRUE(sameDirection(shaped.mNormals[1], weighed));
            EXPECT_EQ(shaped.mNormals[2], up) << "a flat group keeps its normal";
            EXPECT_EQ(shaped.mNormals[3], up);
            // As its word reads back, the form the pass hands every normal over in: (0, −1, 0.2) / √1.04 is no word's.
            const osg::Vec3f bentWord = Shaders::unpackNormal(Shaders::packNormal(bent));
            EXPECT_NE(bentWord, bent);
            EXPECT_EQ(shaped.mNormals[4], bentWord) << "and so does the bevel's lower edge, bent as it is";
            EXPECT_EQ(shaped.mNormals[5], bentWord);
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
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f), // the floor, facing +z
                osg::Vec3f(0.0f, 1.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 1.0f),
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

        /// **A crease between two shapes is cut, and only the mesh's own corners are written.** The same floor and
        /// wall as above as two shapes, as a Morrowind model is one shape a texture: the floor alone holds no hard
        /// edge, so its corners along the wall keep leaning forty-five degrees. With the wall beside it the right
        /// angle is found and the floor's two corners there take the floor's normal, on the four vertices it had; the
        /// wall is read and never written. A floor beside it that carries on flat is no crease.
        TEST(RtxCreaseSplitTest, aCreaseBetweenTwoShapesIsCutOnTheMeshAlone)
        {
            const osg::Vec3f averaged = osg::Vec3f(0.0f, -1.0f, 1.0f) / std::sqrt(2.0f);
            const osg::Vec3f floorNormal(0.0f, 0.0f, 1.0f);
            const osg::Vec3f wallNormal(0.0f, -1.0f, 0.0f);
            const std::array<osg::Vec3f, 4> floor{ osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f) };
            const std::array<osg::Vec3f, 4> floorNormals{ floorNormal, floorNormal, averaged, averaged };
            const std::array<std::uint32_t, 6> triangles{ 0, 1, 2, 0, 2, 3 };

            const std::array<osg::Vec3f, 4> wall{ osg::Vec3f(0.0f, 1.0f, 0.0f), osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 1.0f), osg::Vec3f(0.0f, 1.0f, 1.0f) };
            const std::array<osg::Vec3f, 4> wallNormals{ averaged, averaged, wallNormal, wallNormal };

            const std::array<osg::Vec3f, 4> onward{ osg::Vec3f(0.0f, 1.0f, 0.0f), osg::Vec3f(1.0f, 1.0f, 0.0f),
                osg::Vec3f(1.0f, 2.0f, 0.0f), osg::Vec3f(0.0f, 2.0f, 0.0f) };
            const std::array<osg::Vec3f, 4> onwardNormals{ averaged, averaged, floorNormal, floorNormal };

            ContentPreprocessor content;
            EXPECT_TRUE(shape(content, floor, floorNormals, triangles).mNormals.empty())
                << "the floor alone has no hard edge";

            const Shaped cut = shape(content, floor, floorNormals, triangles, true,
                CreaseSplit::Beside{ .mPositions = wall, .mNormals = wallNormals, .mTriangles = triangles });
            ASSERT_EQ(cut.mNormals.size(), 4u) << "only the floor's own vertices are written";
            EXPECT_TRUE(cut.mSources.empty()) << "each corner along the wall is in the floor's piece alone";
            EXPECT_EQ(cut.mKept, std::vector<std::uint32_t>(triangles.begin(), triangles.end()));
            EXPECT_TRUE(sameDirection(cut.mNormals[2], floorNormal));
            EXPECT_TRUE(sameDirection(cut.mNormals[3], floorNormal));
            EXPECT_EQ(cut.mNormals[0], floorNormal) << "a vertex off the crease moved";
            EXPECT_EQ(cut.mNormals[1], floorNormal);

            EXPECT_TRUE(shape(content, floor, floorNormals, triangles, true,
                CreaseSplit::Beside{ .mPositions = onward, .mNormals = onwardNormals, .mTriangles = triangles })
                    .mNormals.empty())
                << "a flat floor beside it is no crease";
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
        /// triangles, whether it may split, and the faces beside it — the cube's eight positions and
        /// eight normals of twelve bytes, thirty-six indices of four, the flag's one byte, and
        /// nothing beside it: 96 + 96 + 144 + 1. The flag alone moves the key, and so does a beside.
        TEST(RtxCreaseSplitTest, theShapePassIsKeyedOnTheNormalsWhetherItSplitsAndWhatIsBesideIt)
        {
            const AveragedCube cube;
            const auto keyOf = [&](const bool splits, ContentStore::Digest& digest) {
                ShapePass::digest(ShapePass::Input{ .mPositions = cube.mPositions,
                                      .mNormals = cube.mNormals,
                                      .mTriangles = cube.mTriangles,
                                      .mSplits = splits },
                    digest);
                return digest.getKey();
            };

            ContentStore::Digest splitting("shape", ShapePass::sVersion);
            const ContentStore::Key split = keyOf(true, splitting);
            EXPECT_EQ(splitting.getBytes(), 96u + 96u + 144u + 1u);

            ContentStore::Digest whole("shape", ShapePass::sVersion);
            EXPECT_NE(keyOf(false, whole), split);

            // And what stands beside the shape, which a cube's own corners are, here.
            ContentStore::Digest besideIt("shape", ShapePass::sVersion);
            ShapePass::digest(ShapePass::Input{ .mPositions = cube.mPositions,
                                  .mNormals = cube.mNormals,
                                  .mTriangles = cube.mTriangles,
                                  .mSplits = true,
                                  .mBeside = CreaseSplit::Beside{ .mPositions = cube.mPositions,
                                      .mNormals = cube.mNormals,
                                      .mTriangles = cube.mTriangles } },
                besideIt);
            EXPECT_NE(besideIt.getKey(), split);
        }

        /// **A face is read one way by every shape pass**: the unit normal of the corners' cross
        /// product and its length, twice the area. Hand-placed: legs of two and three along x and y
        /// cross to six along z, so the unit is z and the area three; three points on a line cross
        /// to nothing, which is no normal at all rather than a division by nought.
        TEST(RtxFaceCrossTest, aFaceIsItsUnitNormalAndTwiceItsArea)
        {
            const FaceCross face = FaceCross::of(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(2.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 3.0f, 0.0f));
            EXPECT_EQ(face.mUnit, osg::Vec3f(0.0f, 0.0f, 1.0f));
            EXPECT_EQ(face.mTwiceArea, 6.0f);

            const FaceCross line = FaceCross::of(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(1.0f, 0.0f, 0.0f), osg::Vec3f(2.0f, 0.0f, 0.0f));
            EXPECT_EQ(line.mUnit, osg::Vec3f());
            EXPECT_EQ(line.mTwiceArea, 0.0f);
        }

        /// The probe table is a power of two at least twice as long as what it holds and at least
        /// sixteen, and a probe wraps from its last slot to its first: three entries take sixteen
        /// slots, a hash past them is masked, and the slot after fifteen is nought.
        TEST(RtxProbeTableTest, aTableIsTwiceItsEntriesAndAProbeWraps)
        {
            ProbeTable table;
            table.reset(3);
            EXPECT_EQ(table.first(17), 1u);
            EXPECT_EQ(table.next(15), 0u);
            EXPECT_EQ(table[15], ProbeTable::sEmpty);

            table.reset(20);
            EXPECT_EQ(table.first(63), 63u) << "twenty entries take sixty-four slots";
            EXPECT_EQ(table.next(63), 0u);
        }
    }
}
