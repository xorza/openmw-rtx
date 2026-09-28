#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/GL>
#include <osg/Image>
#include <osg/PrimitiveSet>
#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/Vec4ub>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/fakeland.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/rtx/mirror/cells/groundreader.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/terrain/buffercache.hpp>
#include <components/terrain/defs.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx::Testing
{
    namespace
    {
        /// One triangle list against the other, index for index.
        void expectSameTriangles(const std::span<const std::uint32_t> read, const osg::DrawElements& built)
        {
            ASSERT_EQ(read.size(), built.getNumIndices());
            for (std::size_t at = 0; at < read.size(); ++at)
                ASSERT_EQ(read[at], built.index(at)) << "index " << at;
        }

        /// A cell with a land record is the storage's own grid, cut the way `Terrain::BufferCache`
        /// cuts a chunk with no neighbour at another level, with the layers and the transforms the
        /// chunk manager's passes would have carried.
        TEST(RtxGroundReaderTest, aCellIsTheGamesOwnGridCutTheGamesOwnWay)
        {
            FakeLand land;
            land.mWithData = { osg::Vec2i(0, 0) };
            GroundReader reader(land, ESM::Cell::sDefaultWorldspaceId);

            PreparedGround ground;
            reader.read(osg::Vec2i(0, 0), ground);

            EXPECT_TRUE(ground.mStands);
            EXPECT_EQ(ground.mOrigin, osg::Vec3f(0.5f * FakeLand::sCellSize, 0.5f * FakeLand::sCellSize, 0.0f));

            constexpr std::size_t verts = FakeLand::sVerts;
            ASSERT_EQ(ground.mPositions.size(), verts * verts);
            ASSERT_EQ(ground.mNormals.size(), verts * verts);
            ASSERT_EQ(ground.mTexCoords.size(), verts * verts);
            ASSERT_EQ(ground.mColours.size(), verts * verts);

            // Column 64, row 0: the eastern edge at the southern corner, by hand.
            EXPECT_EQ(ground.mPositions[64 * verts], osg::Vec3f(4096.0f, -4096.0f, 8.0f * 64));
            EXPECT_EQ(ground.mPositions[64 * verts], FakeLand::positionAt(64, 0));

            // **The land's `VCLR`, decoded to light and in the storage's own vertex order.** The
            // same vertex carries 64, 128 and 255, which the sRGB curve takes to 0.05126946,
            // 0.21586050 and one — a colour a renderer that dropped the array would answer white
            // for, and one whose channels differ so that a decode that swapped them would show.
            EXPECT_EQ(FakeLand::colourAt(64, 0), osg::Vec4ub(64, 128, 255, 255));
            const osg::Vec3f& corner = ground.mColours[64 * verts];
            EXPECT_FLOAT_EQ(corner.x(), 0.05126946f);
            EXPECT_FLOAT_EQ(corner.y(), 0.21586050f);
            EXPECT_FLOAT_EQ(corner.z(), 1.0f);

            // **The game's own index buffer and the game's own corners**, from the class that
            // builds them for the rasterizer's chunks.
            Terrain::BufferCache buffers;
            expectSameTriangles(ground.mIndices, *buffers.getIndexBuffer(verts, 0));
            EXPECT_EQ(ground.mIndices.size(), (verts - 1) * (verts - 1) * 6);

            const osg::ref_ptr<osg::Vec2Array> corners = buffers.getUVBuffer(verts);
            ASSERT_EQ(corners->size(), ground.mTexCoords.size());
            for (std::size_t at = 0; at < corners->size(); ++at)
                ASSERT_EQ((*corners)[at], ground.mTexCoords[at]) << "corner " << at;

            // Two layers, each tiling sixteen times across the cell, each with the western or the
            // eastern half of a 34 × 34 mask.
            ASSERT_EQ(ground.mLayers.size(), 2u);
            const PreparedLayer& grass = ground.mLayers[0];
            const PreparedLayer& rock = ground.mLayers[1];
            ASSERT_EQ(reader.getLayerFiles().size(), 2u) << "one layer's files a layer";
            const GroundReader::LayerFiles& grassFiles = reader.getLayerFiles()[0];
            const GroundReader::LayerFiles& rockFiles = reader.getLayerFiles()[1];
            EXPECT_EQ(grassFiles.mPath, "textures/grass.dds");
            EXPECT_EQ(rockFiles.mPath, "textures/rock_diffusespec.dds");
            EXPECT_EQ(grass.mRow.mDiffuseTransform, osg::Vec4f(16.0f, 16.0f, 0.0f, 0.0f));

            // The rock's maps as the storage named them, its normal map named beside its diffuse
            // and asked for a height; the grass has neither. Named and not opened: the cell reader
            // opens each file once for as long as it holds it, and says whether the map carries
            // the height (`RtxCellReaderTest`).
            EXPECT_TRUE(rock.mDiffuseSpec);
            EXPECT_EQ(rockFiles.mNormalPath, "textures/rock_nh.dds");
            EXPECT_TRUE(rockFiles.mParallax);
            EXPECT_FALSE(grassFiles.mParallax);
            EXPECT_FALSE(grass.mDiffuseSpec);
            EXPECT_TRUE(grassFiles.mNormalPath.empty());

            // `BlendmapTexMat` at sixteen tiles: a scale of 16 / 17 about the centre and a nudge of
            // a quarter texel, which comes to an offset of 0.75 / 17 in x and 0.25 / 17 in y.
            const osg::Vec4f mask = GroundReader::maskTransform(16);
            EXPECT_NEAR(mask.x(), 16.0f / 17.0f, 1e-6f);
            EXPECT_NEAR(mask.y(), 16.0f / 17.0f, 1e-6f);
            EXPECT_NEAR(mask.z(), 0.75f / 17.0f, 1e-6f);
            EXPECT_NEAR(mask.w(), 0.25f / 17.0f, 1e-6f);
            EXPECT_EQ(grass.mRow.mMaskTransform, mask);
            EXPECT_EQ(rock.mRow.mMaskTransform, mask);

            constexpr std::uint32_t side = FakeLand::sMaskSide;
            EXPECT_EQ(grass.mRow.mMaskWidth, side);
            EXPECT_EQ(grass.mRow.mMaskHeight, side);
            EXPECT_EQ(grass.mWeights.mCount, side * side);
            EXPECT_EQ(rock.mWeights.mCount, side * side);
            EXPECT_EQ(rock.mWeights.mOffset, side * side);
            ASSERT_EQ(ground.mWeights.size(), 2u * side * side);

            // Row 0: grass in the first column, rock in the last, by the byte's own reciprocal.
            EXPECT_EQ(ground.mWeights[grass.mWeights.mOffset], 1.0f);
            EXPECT_EQ(ground.mWeights[grass.mWeights.mOffset + side - 1], 0.0f);
            EXPECT_EQ(ground.mWeights[rock.mWeights.mOffset], 0.0f);
            EXPECT_EQ(ground.mWeights[rock.mWeights.mOffset + side - 1], 1.0f);
        }

        /// A cell with no land record is a plane at the default height on four corners, wearing
        /// the default ground.
        TEST(RtxGroundReaderTest, aCellWithNoRecordIsAPlaneAtTheDefaultHeight)
        {
            FakeLand land;
            GroundReader reader(land, ESM::Cell::sDefaultWorldspaceId);

            PreparedGround ground;
            reader.read(osg::Vec2i(1, 0), ground);

            EXPECT_TRUE(ground.mStands);
            EXPECT_EQ(ground.mOrigin, osg::Vec3f(1.5f * FakeLand::sCellSize, 0.5f * FakeLand::sCellSize, 0.0f));

            ASSERT_EQ(ground.mPositions.size(), 4u);
            for (const osg::Vec3f& corner : ground.mPositions)
            {
                EXPECT_EQ(corner.z(), static_cast<float>(ESM::Land::DEFAULT_HEIGHT));
                EXPECT_EQ(std::abs(corner.x()), 0.5f * FakeLand::sCellSize);
                EXPECT_EQ(std::abs(corner.y()), 0.5f * FakeLand::sCellSize);
            }

            // A cell with no record has no `VCLR` either, and white is what tints nothing.
            ASSERT_EQ(ground.mColours.size(), 4u);
            for (const osg::Vec3f& colour : ground.mColours)
                EXPECT_EQ(colour, osg::Vec3f(1.0f, 1.0f, 1.0f));

            Terrain::BufferCache buffers;
            expectSameTriangles(ground.mIndices, *buffers.getIndexBuffer(2, 0));

            ASSERT_EQ(ground.mLayers.size(), 1u);
            ASSERT_EQ(reader.getLayerFiles().size(), 1u);
            EXPECT_EQ(reader.getLayerFiles()[0].mPath, "textures/_land_default.dds");
            EXPECT_EQ(ground.mLayers[0].mWeights.mCount, 0u) << "one ground type covers the cell";
            EXPECT_TRUE(ground.mWeights.empty());
        }

        /// **An ESM4 cell with no land stands nothing, and says so of its layers too.** An ESM4 world
        /// has no default height, so such a cell is left out — and what it reports of its layers is
        /// its own, nothing, and not the cell read before it, which a reader filed into ground it
        /// never stood.
        TEST(RtxGroundReaderTest, anEsm4CellWithNoLandKeepsNoLayersOfTheCellBefore)
        {
            FakeLand land;
            land.mWithData = { osg::Vec2i(0, 0) };
            GroundReader reader(land, ESM::RefId::stringRefId("tamriel"));

            PreparedGround stood;
            reader.read(osg::Vec2i(0, 0), stood);
            ASSERT_TRUE(stood.mStands);
            ASSERT_EQ(reader.getLayerFiles().size(), 2u);

            PreparedGround empty;
            reader.read(osg::Vec2i(3, 0), empty);
            EXPECT_FALSE(empty.mStands);
            EXPECT_TRUE(empty.mLayers.empty());
            EXPECT_TRUE(reader.getLayerFiles().empty()) << "the cell read before this one still answers";
        }

        /// A land whose one cell blends two masks of two formats: the game's own, and one a mod
        /// might ship.
        class TwoFormats final : public FakeLand
        {
        public:
            TwoFormats()
            {
                mWithData = { osg::Vec2i(0, 0) };

                // Sixteen by sixteen, so the game's format carries every byte a texel can hold —
                // and the other one carries them backwards, which is a different picture rather
                // than the same one twice.
                mAlpha->allocateImage(16, 16, 1, GL_ALPHA, GL_UNSIGNED_BYTE);
                mRgba->allocateImage(16, 16, 1, GL_RGBA, GL_UNSIGNED_BYTE);

                for (int row = 0; row < 16; ++row)
                {
                    unsigned char* alphaRow = mAlpha->data(0, row);
                    unsigned char* rgbaRow = mRgba->data(0, row);

                    for (int column = 0; column < 16; ++column)
                    {
                        const auto value = static_cast<unsigned char>(row * 16 + column);
                        alphaRow[column] = value;

                        rgbaRow[column * 4 + 0] = 7;
                        rgbaRow[column * 4 + 1] = 11;
                        rgbaRow[column * 4 + 2] = 13;
                        rgbaRow[column * 4 + 3] = static_cast<unsigned char>(255 - value);
                    }
                }
            }

            void getBlendmaps(float, const osg::Vec2f&, ImageVector& blendmaps, std::vector<Terrain::LayerInfo>& layers,
                ESM::RefId) override
            {
                layers.push_back(Terrain::LayerInfo{ VFS::Path::Normalized("ground0.dds"), {}, false, false });
                layers.push_back(Terrain::LayerInfo{ VFS::Path::Normalized("ground1.dds"), {}, false, false });
                blendmaps.push_back(mAlpha);
                blendmaps.push_back(mRgba);
            }

            osg::ref_ptr<osg::Image> mAlpha = new osg::Image;
            osg::ref_ptr<osg::Image> mRgba = new osg::Image;
        };

        /// A blend map is read exactly as `osg::Image::getColor` reads it, on both paths that read
        /// one.
        ///
        /// **The game's own masks are one byte a texel in `GL_ALPHA`**, and that one format is read
        /// along the row rather than a texel at a time: `getColor` decides on the pixel format and
        /// the data type per texel and builds a `Vec4` to hand back one component of it. Every
        /// other format still takes `getColor`, so a mod's blend map is read as it always was.
        ///
        /// **The two have to agree to the bit.** A weight is what a cell's ground is blended by and
        /// what its composite is baked from, and `getColor` multiplies by a reciprocal where a
        /// divide differs in the last place for 126 of the 256 byte values.
        TEST(RtxGroundReaderTest, aBlendMapReadsTheSameOnTheRowPathAndTheFallback)
        {
            TwoFormats land;
            GroundReader reader(land, ESM::Cell::sDefaultWorldspaceId);

            PreparedGround ground;
            reader.read(osg::Vec2i(0, 0), ground);
            ASSERT_EQ(ground.mLayers.size(), 2u);

            const auto readsAs = [&](const PreparedLayer& layer, const osg::Image& image) {
                ASSERT_EQ(layer.mRow.mMaskWidth, 16u);
                ASSERT_EQ(layer.mRow.mMaskHeight, 16u);
                ASSERT_EQ(layer.mWeights.mCount, 256u);

                const std::span<const float> weights = layer.mWeights.in(std::span<const float>(ground.mWeights));
                for (int row = 0; row < 16; ++row)
                    for (int column = 0; column < 16; ++column)
                        ASSERT_EQ(weights[static_cast<std::size_t>(row) * 16 + column], image.getColor(column, row).a())
                            << "texel " << column << ", " << row << " of " << image.getPixelFormat();
            };

            readsAs(ground.mLayers[0], *land.mAlpha);
            readsAs(ground.mLayers[1], *land.mRgba);

            // And the two ends of the range by hand, which is the one claim `getColor` cannot be
            // asked to make about itself: an empty texel is no weight and a full one is all of it.
            EXPECT_EQ(ground.mWeights[ground.mLayers[0].mWeights.mOffset], 0.0f);
            EXPECT_EQ(ground.mWeights[ground.mLayers[0].mWeights.mOffset + 255], 1.0f);
        }
    }
}
