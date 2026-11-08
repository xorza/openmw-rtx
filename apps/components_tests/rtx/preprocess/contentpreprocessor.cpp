#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/contentstore.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        /// A unit quad and the same four positions again, its back wound the other way on them:
        /// `ShapeFold`'s own card.
        const std::array<osg::Vec3f, 8> sCard{
            osg::Vec3f(0.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 1.0f, 0.0f),
        };

        const std::array<std::uint32_t, 12> sDoubled{ 0, 1, 2, 0, 2, 3, 6, 5, 4, 7, 6, 4 };

        /// Two by two RGBA8, as a file would arrive.
        osg::ref_ptr<osg::Image> makeImage(const std::array<std::uint8_t, 16>& bytes, const char* name)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName(name);
            image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            for (std::size_t at = 0; at < bytes.size(); ++at)
                image->data()[at] = bytes[at];

            return image;
        }

        const std::array<std::uint8_t, 16> sPaint{ 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255 };

        /// The shape pass asked with no normals: the fold alone.
        FoldedShape foldCard(ContentPreprocessor& content, std::vector<std::uint32_t>& kept)
        {
            std::vector<osg::Vec3f> normals;
            std::vector<std::uint32_t> sources;
            ShapePass::Output output{ .mKept = kept, .mNormals = normals, .mSources = sources };
            content.shape(ShapePass::Input{ .mPositions = sCard, .mTriangles = sDoubled, .mSplits = true }, output);
            return output.mShape;
        }

        /// A shape asked through the preprocessor keeps the card's front and says it was a sheet,
        /// and is counted: asked once, found in no cache, and keyed on nothing, since the store keeps
        /// no shape (`ShapePass::sStored`).
        TEST(RtxContentPreprocessorTest, aShapeIsCountedByWhatItRead)
        {
            ContentPreprocessor content;
            std::vector<std::uint32_t> kept;
            const FoldedShape shape = foldCard(content, kept);

            EXPECT_EQ(kept, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 })) << "the front, as the file wrote it";
            EXPECT_TRUE(shape.mSheet);

            const ContentStats stats = content.takeStats();
            const PassStats& shaped = stats.at(ContentPassId::Shape);
            EXPECT_EQ(shaped.mAsked, 1u);
            EXPECT_EQ(shaped.mHits, 0u);
            EXPECT_EQ(shaped.mKeyBytes, 0u);
            EXPECT_EQ(stats.at(ContentPassId::SolidReach).mAsked, 0u) << "a pass not asked counts nothing";
        }

        /// **With no store, every ask runs.** The same shape asked twice is asked twice and found
        /// twice nowhere, and a take leaves nothing counted behind it.
        TEST(RtxContentPreprocessorTest, theSameInputAskedAgainRunsAgainAndATakeEmptiesTheCount)
        {
            ContentPreprocessor content;
            std::vector<std::uint32_t> kept;
            foldCard(content, kept);
            foldCard(content, kept);

            const ContentStats stats = content.takeStats();
            EXPECT_EQ(stats.at(ContentPassId::Shape).mAsked, 2u);
            EXPECT_EQ(stats.at(ContentPassId::Shape).mHits, 0u);
            EXPECT_EQ(stats.at(ContentPassId::Shape).mKeyBytes, 0u);

            EXPECT_EQ(content.takeStats().at(ContentPassId::Shape).mAsked, 0u);
        }

        /// The texture passes through the preprocessor answer what the readings of the described
        /// level do, for an image they read and for one they cannot: a file in RGB 3-3-2, which is
        /// solid by the rule that changes nothing, and worth nothing.
        TEST(RtxContentPreprocessorTest, theTexturePassesAnswerWhatTheDirectReadingsDo)
        {
            ContentPreprocessor content;
            const osg::ref_ptr<osg::Image> painted = makeImage(sPaint, "painted.dds");

            TexelScratch scratch;
            const std::optional<TextureData> finest = describeFinest(*painted, scratch);
            ASSERT_TRUE(finest.has_value());
            EXPECT_EQ(content.reachesSolid(*painted), reachesSolid(*finest));
            EXPECT_TRUE(content.reachesSolid(*painted)) << "its last texel is solid";

            const MeanTexel direct = meanTexel(*finest, scratch);
            const MeanTexel asked = content.meanTexel(*painted);
            EXPECT_EQ(asked.mColour, direct.mColour);
            EXPECT_EQ(asked.mWhole, direct.mWhole);
            EXPECT_EQ(asked.mAlpha, direct.mAlpha);

            osg::ref_ptr<osg::Image> odd = new osg::Image;
            odd->setFileName("odd.dds");
            odd->allocateImage(2, 2, 1, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            EXPECT_TRUE(content.reachesSolid(*odd));
            EXPECT_EQ(content.meanTexel(*odd).mColour, osg::Vec3f());

            const ContentStats stats = content.takeStats();
            EXPECT_EQ(stats.at(ContentPassId::SolidReach).mAsked, 3u);
            EXPECT_EQ(stats.at(ContentPassId::TexelMean).mAsked, 2u);
        }

        /// What a texture pass's key reads of a two-by-two RGBA8 level: whether it described, its
        /// format, its encoding, its extent and its sixteen bytes.
        constexpr std::uint64_t sFinestKeyBytes
            = sizeof(bool) + sizeof(TextureFormat) + sizeof(TextureEncoding) + 2 * sizeof(std::uint32_t) + 16;

        /// **A pass the store keeps is found again**: the texel mean asked twice through a store is
        /// run once and found once, keyed both times on what it read, and answers the same; and a
        /// later session's frame thread, whose reach is memory alone, finds it in a store made on
        /// the same folder, which held it from its start.
        TEST(RtxContentPreprocessorTest, aStoredPassIsFoundAgainInThisSessionAndTheNext)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const osg::ref_ptr<osg::Image> painted = makeImage(sPaint, "painted.dds");
            TexelScratch scratch;
            const MeanTexel direct = meanTexel(*describeFinest(*painted, scratch), scratch);
            {
                ContentStore store(folder, ContentStore::sGameCap);
                ContentPreprocessor content(&store, StoreReach::Disk);
                const MeanTexel run = content.meanTexel(*painted);
                const MeanTexel found = content.meanTexel(*painted);
                EXPECT_EQ(run.mColour, direct.mColour);
                EXPECT_EQ(found.mColour, direct.mColour);
                EXPECT_EQ(found.mWhole, direct.mWhole);
                EXPECT_EQ(found.mAlpha, direct.mAlpha);

                const PassStats stats = content.takeStats().at(ContentPassId::TexelMean);
                EXPECT_EQ(stats.mAsked, 2u);
                EXPECT_EQ(stats.mHits, 1u);
                EXPECT_EQ(stats.mKeyBytes, 2 * sFinestKeyBytes);
            }

            ContentStore next(folder, ContentStore::sGameCap);
            next.flush();
            ContentPreprocessor frame(&next, StoreReach::Memory);
            const MeanTexel found = frame.meanTexel(*painted);
            EXPECT_EQ(found.mColour, direct.mColour);
            EXPECT_EQ(found.mAlpha, direct.mAlpha);
            EXPECT_EQ(frame.takeStats().at(ContentPassId::TexelMean).mHits, 1u);
        }

        /// **A pass the store keeps nothing of makes no key**, store or none: the solid reach, whose
        /// walk stops at the first solid texel, runs every time and writes nothing.
        TEST(RtxContentPreprocessorTest, aPassTheStoreKeepsNothingOfMakesNoKey)
        {
            ContentStore store(TestingOpenMW::currentTestDirPath() / "store", ContentStore::sGameCap);
            ContentPreprocessor content(&store, StoreReach::Disk);
            const osg::ref_ptr<osg::Image> painted = makeImage(sPaint, "painted.dds");
            EXPECT_TRUE(content.reachesSolid(*painted));
            EXPECT_TRUE(content.reachesSolid(*painted));

            const PassStats stats = content.takeStats().at(ContentPassId::SolidReach);
            EXPECT_EQ(stats.mAsked, 2u);
            EXPECT_EQ(stats.mHits, 0u);
            EXPECT_EQ(stats.mKeyBytes, 0u);
            store.flush();
            EXPECT_EQ(store.getStats().mWritten, 0u);
        }

        ContentKey finestKeyOf(const osg::Image& image, FinestTexels& finest)
        {
            ContentDigest digest("solid reach", 1);
            finest.describe(image);
            finest.addTo(digest);
            return digest.getKey();
        }

        /// A texture pass is keyed on the picture and not on the file: the same texels under
        /// another name are one key, and one channel of one texel moved is another. The key reads
        /// the finest level as it was described — format, encoding, extent and its sixteen bytes.
        TEST(RtxContentPreprocessorTest, aTextureIsKeyedOnItsPictureAndNotOnItsName)
        {
            TexelScratch scratch;
            FinestTexels finest(scratch);

            const ContentKey key = finestKeyOf(*makeImage(sPaint, "painted.dds"), finest);
            ASSERT_TRUE(finest.get().has_value());
            EXPECT_EQ(finestKeyOf(*makeImage(sPaint, "elsewhere/renamed.dds"), finest), key);

            std::array<std::uint8_t, 16> moved = sPaint;
            moved[7] = 129;
            EXPECT_NE(finestKeyOf(*makeImage(moved, "painted.dds"), finest), key) << "one texel's alpha";

            // The caller's image gone before the key reads it, which the description's hold keeps:
            // an RGBA8 level is spanned where the image holds it.
            ContentDigest digest("solid reach", 1);
            finest.describe(*makeImage(sPaint, "painted.dds"));
            finest.addTo(digest);
            EXPECT_EQ(digest.getBytes(), sFinestKeyBytes);
            EXPECT_EQ(digest.getKey(), key) << "the description outlived the caller's image";

            const osg::ref_ptr<osg::Image> held = makeImage(sPaint, "painted.dds");
            finest.describe(*held);
            EXPECT_TRUE(finest.describes(*held));
            EXPECT_FALSE(finest.describes(*makeImage(sPaint, "painted.dds"))) << "the same texels, another image";
            finest.clear();
            EXPECT_FALSE(finest.describes(*held));
            EXPECT_FALSE(finest.get().has_value());

            osg::ref_ptr<osg::Image> odd = new osg::Image;
            odd->allocateImage(2, 2, 1, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            finestKeyOf(*odd, finest);
            EXPECT_FALSE(finest.get().has_value()) << "an image no reader decodes is described as none";
        }

        /// **A stored payload its codec cannot read is a miss, and the output run again is held from
        /// then on**: a texel mean filed as three bytes, against the 28 its codec reads, runs, and the
        /// run's output replaces the held payload, so the next ask finds it.
        TEST(RtxContentPreprocessorTest, aPayloadItsCodecCannotReadIsRunAgainAndReplaced)
        {
            ContentStore store(TestingOpenMW::currentTestDirPath() / "store", ContentStore::sGameCap);
            const osg::ref_ptr<osg::Image> painted = makeImage(sPaint, "painted.dds");

            TexelScratch scratch;
            FinestTexels finest(scratch);
            ContentDigest digest(sContentPasses.name(ContentPassId::TexelMean), TexelMean::sVersion);
            finest.describe(*painted);
            finest.addTo(digest);
            const std::vector<std::byte> broken{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
            store.write(digest.getKey(), ContentPassId::TexelMean, TexelMean::sVersion, broken);

            ContentPreprocessor content(&store, StoreReach::Memory);
            const MeanTexel first = content.meanTexel(*painted);
            const MeanTexel second = content.meanTexel(*painted);
            EXPECT_EQ(second.mColour, first.mColour);
            EXPECT_EQ(second.mAlpha, first.mAlpha);

            const PassStats stats = content.takeStats().at(ContentPassId::TexelMean);
            EXPECT_EQ(stats.mAsked, 2u);
            EXPECT_EQ(stats.mHits, 1u) << "the second, from the run's output";
        }

        /// A sum is pass by pass and figure by figure, and the whole is every key and every run:
        /// 1 + 2 + 3 + 4 = 10 ms.
        TEST(RtxContentStatsTest, aSumIsPassByPassAndTheWholeIsEveryKeyAndRun)
        {
            ContentStats first;
            first.at(ContentPassId::Shape)
                = PassStats{ .mAsked = 2, .mHits = 1, .mKeyMs = 1.0, .mRunMs = 2.0, .mKeyBytes = 10 };

            ContentStats second;
            second.at(ContentPassId::Shape) = PassStats{ .mAsked = 1, .mKeyMs = 3.0, .mKeyBytes = 5 };
            second.at(ContentPassId::TexelMean) = PassStats{ .mAsked = 4, .mRunMs = 4.0 };

            Preprocessed both{ .mOnFrame = first };
            both += Preprocessed{ .mOnFrame = second, .mOffFrame = second };

            const PassStats& fold = both.mOnFrame.at(ContentPassId::Shape);
            EXPECT_EQ(fold.mAsked, 3u);
            EXPECT_EQ(fold.mHits, 1u);
            EXPECT_EQ(fold.mKeyMs, 4.0);
            EXPECT_EQ(fold.mRunMs, 2.0);
            EXPECT_EQ(fold.mKeyBytes, 15u);
            EXPECT_EQ(both.mOnFrame.at(ContentPassId::TexelMean).mAsked, 4u);
            EXPECT_EQ(both.mOnFrame.getMs(), 10.0);
            EXPECT_EQ(both.mOffFrame.getMs(), 7.0);
        }
    }
}
