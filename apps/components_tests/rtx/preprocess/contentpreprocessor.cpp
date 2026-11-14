#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC formats, which Windows's gl.h lacks.
#include <osg/Vec3f>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/image.hpp>
#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/contentcache.hpp>
#include <components/rtx/preprocess/contentcodec.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/chainscratch.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>
#include <components/rtx/shaders/tangent.h>
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
            osg::ref_ptr<osg::Image> image = Testing::makeImage(name, 2, 2);
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

        /// `width` by `height` of loose RGBA8, mid grey and opaque, with no chain.
        osg::ref_ptr<osg::Image> makeLoose(int width, int height)
        {
            osg::ref_ptr<osg::Image> image = Testing::makeImage("textures/tx_loose.dds", width, height);
            std::memset(image->data(), 128, image->getTotalSizeInBytes());
            return image;
        }

        /// `width` by `height` of BC1 with `levels` levels, every block a black opaque one, as a file lays
        /// its levels back to back.
        osg::ref_ptr<osg::Image> makeBc1(std::uint32_t width, std::uint32_t height, std::uint32_t levels)
        {
            const TexelLayout layout = layoutOf(TextureFormat::Bc1RgbaSrgb);
            osg::Image::MipmapDataType offsets;
            std::size_t bytes = 0;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                if (level > 0)
                    offsets.push_back(static_cast<unsigned int>(bytes));
                bytes += layout.levelBytes(std::max(width >> level, 1u), std::max(height >> level, 1u));
            }
            auto* data = new unsigned char[bytes]();
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setImage(static_cast<int>(width), static_cast<int>(height), 1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(offsets);
            image->setFileName("textures/tx_block.dds");
            return image;
        }

        /// What a preprocessor and the chain it made hold, as a dispatcher reads a thread's.
        ChainScratch heldBy(const ContentPreprocessor& content, const MadeChain& made)
        {
            ChainScratch held = content.chainScratch();
            held[ChainBuffer::Made] = made.mBytes.capacity();
            return held;
        }

        /// **A chain's scratch is predicted off its image's header, and its buffers grow to no more**: for each file
        /// and encoding, run on a preprocessor that held nothing, every buffer it holds after is within the
        /// prediction, and the buffers that grow with the texels are it exactly. A 32 by 16 loose file as a colour, a
        /// normal map and a painted colour (chainless, built to a texel, six levels, 683 texels, 2732 bytes laid); a
        /// 64-square BC1 of seven levels as a painted colour (every level of its own read), the same with one level
        /// as data (built to a texel), and as a colour with its chain (which stands and builds nothing); and a
        /// conversion either way within its own. Then a 16-square file after a 64-square one grows nothing.
        /// A band is four rows of the finest level at twelve bytes a texel, `32 · 4 · 12 = 1536` and
        /// `64 · 4 · 12 = 3072`, and a normal map reads through none: its ring is eight rows of each level at
        /// sixteen bytes a texel, `8 · (32 + 16 + 8 + 4 + 2 + 1) · 16 = 8064`.
        TEST(RtxContentPreprocessorTest, aChainsScratchIsPredictedOffItsHeaderAndGrowsToNoMore)
        {
            struct Case
            {
                osg::ref_ptr<osg::Image> mImage;
                TextureEncoding mEncoding;
                std::size_t mTexels;
                std::size_t mBand;
                std::size_t mRing;
            };
            const std::vector<Case> cases{
                { makeLoose(32, 16), TextureEncoding::Colour, 2732, 1536, 0 },
                { makeLoose(32, 16), TextureEncoding::Normal, 2732, 0, 8064 },
                { makeLoose(32, 16), TextureEncoding::Painted, 2732, 1536, 0 },
                { makeBc1(64, 64, 7), TextureEncoding::Painted, 21844, 3072, 0 },
                { makeBc1(64, 64, 1), TextureEncoding::Data, 21844, 3072, 0 },
                { makeBc1(64, 64, 7), TextureEncoding::Colour, 0, 0, 0 },
            };
            for (const Case& tried : cases)
            {
                const ChainPass::Input input{ .mImage = *tried.mImage, .mEncoding = tried.mEncoding };
                ContentPreprocessor content(nullptr);
                MadeChain made;
                content.chain(input, made);
                const ChainScratch predicted = ChainPass::scratchOf(input);
                const ChainScratch held = heldBy(content, made);
                const auto encoding = static_cast<int>(tried.mEncoding);
                EXPECT_TRUE(held.within(predicted)) << encoding;
                EXPECT_EQ(predicted[ChainBuffer::Texels], tried.mTexels) << encoding;
                EXPECT_EQ(predicted[ChainBuffer::Band], tried.mBand) << encoding;
                EXPECT_EQ(predicted[ChainBuffer::Ring], tried.mRing) << encoding;
                for (const ChainBuffer exact : { ChainBuffer::File, ChainBuffer::Alpha, ChainBuffer::Texels,
                         ChainBuffer::Band, ChainBuffer::Ring, ChainBuffer::Columns })
                    EXPECT_EQ(held[exact], predicted[exact]) << encoding << " " << static_cast<int>(exact);
                EXPECT_EQ(held[ChainBuffer::Payload], 0u) << "the store frees the payload";
            }

            // And a conversion, both ways, of a 64-square BC1 diffuse and a 32 by 16 loose `_spec`.
            const osg::ref_ptr<osg::Image> diffuse = makeBc1(64, 64, 7);
            const osg::ref_ptr<osg::Image> specular = makeLoose(32, 16);
            for (const TextureEncoding encoding :
                { TextureEncoding::ConvertedColour, TextureEncoding::ConvertedSpecular })
            {
                const bool colour = encoding == TextureEncoding::ConvertedColour;
                const ConversionPass::Input input{ .mImage = colour ? *diffuse : *specular,
                    .mPartner = colour ? *specular : *diffuse,
                    .mEncoding = encoding };
                ContentPreprocessor content(nullptr);
                MadeChain made;
                content.convert(input, made);
                ASSERT_NE(made.mForm, ChainForm::Stands) << static_cast<int>(encoding);
                EXPECT_TRUE(heldBy(content, made).within(ConversionPass::scratchOf(input)))
                    << static_cast<int>(encoding);
            }

            ContentPreprocessor content(nullptr);
            MadeChain made;
            content.chain(
                ChainPass::Input{ .mImage = *makeBc1(64, 64, 1), .mEncoding = TextureEncoding::Painted }, made);
            const ChainScratch afterLarge = heldBy(content, made);
            const osg::ref_ptr<osg::Image> smallImage = makeBc1(16, 16, 1);
            const ChainPass::Input small{ .mImage = *smallImage, .mEncoding = TextureEncoding::Painted };
            const ChainScratch predicted = ChainPass::scratchOf(small);
            EXPECT_EQ(afterLarge.growthTo(predicted), predicted[ChainBuffer::Payload]) << "the payload alone";
            content.chain(small, made);
            EXPECT_EQ(heldBy(content, made).mBytes, afterLarge.mBytes) << "a smaller file grows nothing";
        }

        /// A shape asked through the preprocessor keeps the card's front and says it was a sheet,
        /// and is counted: asked once, found in no cache, and keyed on nothing, since there is no
        /// store.
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

        /// **A shape is found again through the store, in this session and the next**: the card folded
        /// through a store is run once and found once, and a later session's reader finds its front
        /// and its sheet in the file.
        TEST(RtxContentPreprocessorTest, aShapeIsFoundAgainThroughTheStore)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            {
                ContentStore::Store store(folder, sContentStorePolicy);
                ContentPreprocessor content(&store, ContentStore::Reach::Disk);
                std::vector<std::uint32_t> kept;
                foldCard(content, kept);
                kept.clear();
                EXPECT_TRUE(foldCard(content, kept).mSheet);
                EXPECT_EQ(kept, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 }));
                const PassStats stats = content.takeStats().at(ContentPassId::Shape);
                EXPECT_EQ(stats.mAsked, 2u);
                EXPECT_EQ(stats.mHits, 1u);
                EXPECT_GT(stats.mFoundMs, 0.0) << "the hit's read and decode not counted";
            }

            ContentStore::Store next(folder, sContentStorePolicy);
            ContentPreprocessor reader(&next, ContentStore::Reach::Disk);
            std::vector<std::uint32_t> kept;
            EXPECT_TRUE(foldCard(reader, kept).mSheet);
            EXPECT_EQ(kept, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 }));
            EXPECT_EQ(reader.takeStats().at(ContentPassId::Shape).mHits, 1u);
        }

        /// **A shape's codec writes every field and reads back exactly one**: kept triangles, normals,
        /// the added vertices' sources and the three findings come back as they went, the indices
        /// sixteen bits each under 65536 and the normals as the device's words read back, and a payload
        /// a byte short, a byte long or with a fifth flag is no shape. A vertex at 65536 files every
        /// index at thirty-two bits.
        TEST(RtxContentCodecTest, aShapeIsReadBackAsItWentAndNothingElseIsAShape)
        {
            std::vector<std::uint32_t> kept{ 0, 1, 2, 2, 1, 3 };
            std::vector<osg::Vec3f> normals{ osg::Vec3f(0, 0, 1), osg::Vec3f(0, 1, 0), osg::Vec3f(1, 0, 0),
                osg::Vec3f(0.5f, 0.25f, -1) };
            std::vector<std::uint32_t> sources{ 2 };
            const ShapePass::Output written{ .mKept = kept,
                .mNormals = normals,
                .mSources = sources,
                .mShape = FoldedShape{ .mSheet = false, .mFolded = true, .mPocketed = true } };
            Misc::ByteBuffer payload;
            ContentCodec<ShapePass::Output>::encode(written, payload);
            // 1 + (4 + 6 × 2) + (4 + 4 × 4) + (4 + 2)
            EXPECT_EQ(payload.size(), 43u);

            std::vector<std::uint32_t> readKept{ 9 };
            std::vector<osg::Vec3f> readNormals;
            std::vector<std::uint32_t> readSources{ 7, 7 };
            ShapePass::Output read{ .mKept = readKept, .mNormals = readNormals, .mSources = readSources };
            ASSERT_TRUE(ContentCodec<ShapePass::Output>::decode(payload, read));
            EXPECT_EQ(readKept, kept);
            ASSERT_EQ(readNormals.size(), normals.size());
            for (std::size_t at = 0; at < normals.size(); ++at)
                EXPECT_EQ(readNormals[at], Shaders::unpackNormal(Shaders::packNormal(normals[at]))) << at;
            EXPECT_EQ(readNormals[0], osg::Vec3f(0, 0, 1)) << "an axis is a word exactly";
            EXPECT_EQ(readSources, sources);
            EXPECT_FALSE(read.mShape.mSheet);
            EXPECT_TRUE(read.mShape.mFolded);
            EXPECT_TRUE(read.mShape.mPocketed);

            EXPECT_FALSE(ContentCodec<ShapePass::Output>::decode(std::span(payload).first(payload.size() - 1), read));
            Misc::ByteBuffer longer = payload;
            longer.push_back(std::byte{ 0 });
            EXPECT_FALSE(ContentCodec<ShapePass::Output>::decode(longer, read));
            Misc::ByteBuffer fifth{ std::byte{ 16 | 8 } };
            fifth.insert(fifth.end(), payload.begin() + 1, payload.end());
            EXPECT_FALSE(ContentCodec<ShapePass::Output>::decode(fifth, read));

            std::vector<std::uint32_t> wide{ 0, 1, 65536 };
            std::vector<osg::Vec3f> noWideNormals;
            std::vector<std::uint32_t> noWideSources;
            Misc::ByteBuffer widePayload;
            ContentCodec<ShapePass::Output>::encode(
                ShapePass::Output{ .mKept = wide, .mNormals = noWideNormals, .mSources = noWideSources }, widePayload);
            EXPECT_EQ(widePayload.size(), 1u + (4 + 3 * 4) + 4 + 4);
            ASSERT_TRUE(ContentCodec<ShapePass::Output>::decode(widePayload, read));
            EXPECT_EQ(readKept, wide);

            // **And a shape with no normals and no added vertex**, two empty runs: 1 + (4 + 12) + 4 + 4.
            std::vector<osg::Vec3f> noNormals;
            std::vector<std::uint32_t> noSources;
            Misc::ByteBuffer bare;
            ContentCodec<ShapePass::Output>::encode(
                ShapePass::Output{ .mKept = kept, .mNormals = noNormals, .mSources = noSources }, bare);
            EXPECT_EQ(bare.size(), 25u);
            ASSERT_TRUE(ContentCodec<ShapePass::Output>::decode(bare, read));
            EXPECT_EQ(readKept, kept);
            EXPECT_TRUE(readNormals.empty());
            EXPECT_TRUE(readSources.empty());
        }

        /// **A made chain's payload is its bytes, then its form, and a read takes it whole**: three bytes
        /// in BC7 encode as those three and the form's byte, 2 (`ChainForm::Bc7`), and decode back into
        /// the very buffer the payload was, which is left empty, so a stored chain is never copied out
        /// of what the store read. A form past `ChainForm::Own`, and an empty payload, are no chain.
        TEST(RtxContentCodecTest, aMadeChainTakesItsPayloadWhole)
        {
            const MadeChain written{ .mForm = ChainForm::Bc7,
                .mBytes = Misc::ByteBuffer{ std::byte{ 7 }, std::byte{ 8 }, std::byte{ 9 } } };
            Misc::ByteBuffer payload;
            ContentCodec<MadeChain>::encode(written, payload);
            EXPECT_EQ(payload,
                (Misc::ByteBuffer{ std::byte{ 7 }, std::byte{ 8 }, std::byte{ 9 },
                    std::byte{ static_cast<std::uint8_t>(ChainForm::Bc7) } }));

            const std::byte* const held = payload.data();
            MadeChain read;
            ASSERT_TRUE(ContentCodec<MadeChain>::decode(payload, read));
            EXPECT_EQ(read.mForm, ChainForm::Bc7);
            EXPECT_EQ(read.mBytes, written.mBytes);
            EXPECT_EQ(read.mBytes.data(), held) << "the chain's bytes are the payload's buffer";
            EXPECT_TRUE(payload.empty());

            Misc::ByteBuffer pastOwn{ std::byte{ 7 }, std::byte{ static_cast<std::uint8_t>(ChainForm::Own) + 1 } };
            EXPECT_FALSE(ContentCodec<MadeChain>::decode(pastOwn, read));
            EXPECT_EQ(pastOwn.size(), 2u) << "a payload that is no chain is left as it was";
            Misc::ByteBuffer empty;
            EXPECT_FALSE(ContentCodec<MadeChain>::decode(empty, read));
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

            osg::ref_ptr<osg::Image> odd = Testing::makeImage("odd.dds", 2, 2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            EXPECT_TRUE(content.reachesSolid(*odd));
            EXPECT_EQ(content.meanTexel(*odd).mColour, osg::Vec3f());

            const ContentStats stats = content.takeStats();
            EXPECT_EQ(stats.at(ContentPassId::SolidReach).mAsked, 3u);
            EXPECT_EQ(stats.at(ContentPassId::TexelMean).mAsked, 2u);
        }

        /// **A `_spec` map's layout is its channels' spread**: four texels of `(100, 100, 125)` spread
        /// `25 / 255 = 0.098` on the mean, under `sClassicSpreadMost`, so classic; of `(100, 100, 126)`,
        /// `26 / 255 = 0.102`, metal-roughness; the painted test texture, whose channels differ,
        /// metal-roughness; and a file no reader decodes metal-roughness, read as the data it would be.
        TEST(RtxContentPreprocessorTest, aSpecularMapsLayoutIsItsChannelsSpread)
        {
            ContentPreprocessor content;
            const auto texels = [](std::uint8_t blue) {
                std::array<std::uint8_t, 16> bytes{};
                for (std::size_t texel = 0; texel < 4; ++texel)
                {
                    bytes[texel * 4] = 100;
                    bytes[texel * 4 + 1] = 100;
                    bytes[texel * 4 + 2] = blue;
                    bytes[texel * 4 + 3] = 255;
                }
                return bytes;
            };
            EXPECT_EQ(content.specularLayout(*makeImage(texels(125), "under.dds")), SpecularLayout::Classic);
            EXPECT_EQ(content.specularLayout(*makeImage(texels(126), "over.dds")), SpecularLayout::MetalRoughness);
            EXPECT_EQ(content.specularLayout(*makeImage(sPaint, "painted.dds")), SpecularLayout::MetalRoughness);

            osg::ref_ptr<osg::Image> odd = Testing::makeImage("odd.dds", 2, 2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            EXPECT_EQ(content.specularLayout(*odd), SpecularLayout::MetalRoughness);
            EXPECT_EQ(content.takeStats().at(ContentPassId::SpecularKind).mAsked, 4u);
        }

        /// **Khronos' converter, texel by texel**, every colour in linear light:
        /// - a highlight under a dielectric's 0.04, `(0.02, 0.02, 0.02)`, over a diffuse of 0.5 is no metal,
        ///   its base the diffuse over what the highlight leaves, `0.5 · 0.98 / 0.96 = 0.510417`;
        /// - a white highlight over black is `b = 1 − 0.08 = 0.92`, `c = −0.96`, a discriminant of
        ///   `0.8464 + 0.1536 = 1`, so a metalness of `(−0.92 + 1) / 0.08 = 1` and a base of the highlight;
        /// - a grey highlight of 0.5 over a diffuse of 0.2, both their own perceived brightness, is
        ///   `b = 0.2 · 0.5 / 0.96 + 0.5 − 0.08 = 0.524167`, `c = −0.46`, so a metalness of
        ///   `(−0.524167 + sqrt(0.348351)) / 0.08 = 0.825575`, and both the diffuse's base,
        ///   `0.2 · 0.5 / 0.96 / 0.174425 = 0.597200`, and the highlight's,
        ///   `(0.5 − 0.04 · 0.174425) / 0.825575 = 0.597187`, come to 0.5972.
        TEST(RtxContentPreprocessorTest, aClassicTexelIsConvertedAsKhronosConvertsIt)
        {
            const ConvertedTexel dielectric
                = convertTexel(osg::Vec3f(0.5f, 0.5f, 0.5f), osg::Vec3f(0.02f, 0.02f, 0.02f));
            EXPECT_EQ(dielectric.mMetal, 0.0f);
            EXPECT_NEAR(dielectric.mBase.x(), 0.510417f, 1e-5f);

            const ConvertedTexel metal = convertTexel(osg::Vec3f(), osg::Vec3f(1.0f, 1.0f, 1.0f));
            EXPECT_NEAR(metal.mMetal, 1.0f, 1e-5f);
            EXPECT_NEAR(metal.mBase.y(), 1.0f, 1e-5f);

            const ConvertedTexel between = convertTexel(osg::Vec3f(0.2f, 0.2f, 0.2f), osg::Vec3f(0.5f, 0.5f, 0.5f));
            EXPECT_NEAR(between.mMetal, 0.825575f, 1e-5f);
            EXPECT_NEAR(between.mBase.z(), 0.5972f, 1e-4f);
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
                ContentStore::Store store(folder, sContentStorePolicy);
                ContentPreprocessor content(&store, ContentStore::Reach::Disk);
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

            ContentStore::Store next(folder, sContentStorePolicy);
            next.flush();
            ContentPreprocessor frame(&next, ContentStore::Reach::Memory);
            const MeanTexel found = frame.meanTexel(*painted);
            EXPECT_EQ(found.mColour, direct.mColour);
            EXPECT_EQ(found.mAlpha, direct.mAlpha);
            EXPECT_EQ(frame.takeStats().at(ContentPassId::TexelMean).mHits, 1u);
        }

        /// **A chain asked only to be stored is run where the store lacks it, and not read where it holds
        /// it**: the first ask runs and files the chain, its bytes moved into the store's payload and not
        /// copied, and a later one, in a session of its own, counts a hit and leaves the output it was handed
        /// as it was, a coarser chain of one byte, nine; an ask for the output then reads the bytes a run with
        /// no store makes.
        TEST(RtxContentPreprocessorTest, aChainAskedOnlyToBeStoredIsNotReadWhereTheStoreHoldsIt)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "store";
            const osg::ref_ptr<osg::Image> painted = makeImage(sPaint, "painted.dds");
            const ChainPass::Input input{ .mImage = *painted, .mEncoding = TextureEncoding::Normal };
            MadeChain made;
            ContentPreprocessor(nullptr, ContentStore::Reach::Disk).chain(input, made);
            ASSERT_FALSE(made.mBytes.empty());
            {
                ContentStore::Store store(folder, sContentStorePolicy);
                ContentPreprocessor content(&store, ContentStore::Reach::Disk);
                MadeChain stored;
                content.chain(input, stored, ContentAsk::Stored);
                ASSERT_EQ(stored.mForm, ChainForm::Bc7) << "the store lacked it, so it ran";
                EXPECT_TRUE(stored.mBytes.empty()) << "the chain was copied into the store's payload";
                EXPECT_EQ(content.takeStats().at(ContentPassId::Chain).mHits, 0u);
            }

            ContentStore::Store next(folder, sContentStorePolicy);
            ContentPreprocessor content(&next, ContentStore::Reach::Disk);
            MadeChain untouched{ .mForm = ChainForm::Coarser, .mBytes = Misc::ByteBuffer{ std::byte{ 9 } } };
            content.chain(input, untouched, ContentAsk::Stored);
            EXPECT_EQ(untouched.mForm, ChainForm::Coarser);
            EXPECT_EQ(untouched.mBytes, Misc::ByteBuffer{ std::byte{ 9 } });
            EXPECT_EQ(content.takeStats().at(ContentPassId::Chain).mHits, 1u);

            MadeChain read;
            content.chain(input, read);
            EXPECT_EQ(read.mForm, ChainForm::Bc7);
            EXPECT_EQ(read.mBytes, made.mBytes);
            EXPECT_EQ(content.takeStats().at(ContentPassId::Chain).mHits, 1u);

            // **And a hit lets go of the image its key was read from**, as a run does: a file the
            // scene let go of is not held by the thread that asked for its chain.
            osg::ref_ptr<osg::Image> again = makeImage(sPaint, "painted.dds");
            const osg::observer_ptr<osg::Image> watched(again);
            content.chain(ChainPass::Input{ .mImage = *again, .mEncoding = TextureEncoding::Normal }, read);
            EXPECT_EQ(content.takeStats().at(ContentPassId::Chain).mHits, 1u);
            again = nullptr;
            osg::ref_ptr<osg::Image> standing;
            EXPECT_FALSE(watched.lock(standing)) << "the image the hit read its key from was held";
        }

        /// **A pass the store keeps nothing of makes no key**, store or none: the solid reach, whose
        /// walk stops at the first solid texel, runs every time and writes nothing.
        TEST(RtxContentPreprocessorTest, aPassTheStoreKeepsNothingOfMakesNoKey)
        {
            ContentStore::Store store(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            ContentPreprocessor content(&store, ContentStore::Reach::Disk);
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

        ContentStore::Key finestKeyOf(const osg::Image& image, FinestTexels& finest)
        {
            ContentStore::Digest digest("solid reach", 1);
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

            const ContentStore::Key key = finestKeyOf(*makeImage(sPaint, "painted.dds"), finest);
            ASSERT_TRUE(finest.get().has_value());
            EXPECT_EQ(finestKeyOf(*makeImage(sPaint, "elsewhere/renamed.dds"), finest), key);

            std::array<std::uint8_t, 16> moved = sPaint;
            moved[7] = 129;
            EXPECT_NE(finestKeyOf(*makeImage(moved, "painted.dds"), finest), key) << "one texel's alpha";

            // The caller's image gone before the key reads it, which the description's hold keeps:
            // an RGBA8 level is spanned where the image holds it.
            ContentStore::Digest digest("solid reach", 1);
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

            osg::ref_ptr<osg::Image> odd = Testing::makeImage("", 2, 2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            finestKeyOf(*odd, finest);
            EXPECT_FALSE(finest.get().has_value()) << "an image no reader decodes is described as none";
        }

        /// **A stored payload its codec cannot read is a miss, and the output run again is filed in its
        /// place**: a texel mean filed as three bytes, against the 28 its codec reads, runs, and the
        /// run's output replaces the held payload, so the next ask finds it, and the file, so the next
        /// session reads the 28.
        TEST(RtxContentPreprocessorTest, aPayloadItsCodecCannotReadIsRunAgainAndReplaced)
        {
            ContentStore::Store store(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            const osg::ref_ptr<osg::Image> painted = makeImage(sPaint, "painted.dds");

            TexelScratch scratch;
            FinestTexels finest(scratch);
            ContentStore::Digest digest(sContentPasses.name(ContentPassId::TexelMean), TexelMean::sVersion);
            finest.describe(*painted);
            finest.addTo(digest);
            const Misc::ByteBuffer broken{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
            store.write(digest.getKey(), storeKindOf(ContentPassId::TexelMean), TexelMean::sVersion, broken,
                ContentStore::Reach::Disk);

            ContentPreprocessor content(&store, ContentStore::Reach::Memory);
            const MeanTexel first = content.meanTexel(*painted);
            const MeanTexel second = content.meanTexel(*painted);
            EXPECT_EQ(second.mColour, first.mColour);
            EXPECT_EQ(second.mAlpha, first.mAlpha);

            const PassStats stats = content.takeStats().at(ContentPassId::TexelMean);
            EXPECT_EQ(stats.mAsked, 2u);
            EXPECT_EQ(stats.mHits, 1u) << "the second, from the run's output";

            store.flush();
            Misc::ByteBuffer filed;
            ContentStore::Store next(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            ASSERT_TRUE(next.read(digest.getKey(), storeKindOf(ContentPassId::TexelMean), TexelMean::sVersion,
                ContentStore::Reach::Disk, filed));
            EXPECT_EQ(filed.size(), 28u) << "the broken file stood";
        }

        /// **A stored shape that reads whole and does not fit its input is run again and replaced**: the digest says
        /// a file is intact, not that it is this input's, and a misfit in the mesh table would write or read past the
        /// mesh's run. The card has eight vertices: a kept corner of 8 with no vertex added, a triangle cut short, an
        /// added vertex copying vertex 8, and normals for seven of eight vertices are each a miss, the card's front
        /// kept as a run keeps it, and the run's output filed in the misfit's place.
        TEST(RtxContentPreprocessorTest, aStoredShapeThatDoesNotFitItsInputIsRunAgain)
        {
            const ShapePass::Input input{ .mPositions = sCard, .mTriangles = sDoubled, .mSplits = true };
            ContentStore::Digest digest(sContentPasses.name(ContentPassId::Shape), ShapePass::sVersion);
            ShapePass::digest(input, digest);

            struct Misfit
            {
                std::string_view mName;
                std::vector<std::uint32_t> mKept;
                std::vector<osg::Vec3f> mNormals;
                std::vector<std::uint32_t> mSources;
            };
            const std::vector<Misfit> misfits{
                { "a corner past the vertices", { 0, 1, 8 }, {}, {} },
                { "a triangle cut short", { 0, 1, 2, 0 }, {}, {} },
                { "an added vertex copying none of the input's", { 0, 1, 8 }, {}, { 8 } },
                { "normals for seven of eight vertices", { 0, 1, 2 }, std::vector<osg::Vec3f>(7), {} },
            };
            for (std::size_t at = 0; at < misfits.size(); ++at)
            {
                const Misfit& misfit = misfits[at];
                std::vector<std::uint32_t> kept = misfit.mKept;
                std::vector<osg::Vec3f> normals = misfit.mNormals;
                std::vector<std::uint32_t> sources = misfit.mSources;
                Misc::ByteBuffer payload;
                ContentCodec<ShapePass::Output>::encode(
                    ShapePass::Output{ .mKept = kept, .mNormals = normals, .mSources = sources }, payload);
                EXPECT_FALSE(ShapePass::fits(
                    input, ShapePass::Output{ .mKept = kept, .mNormals = normals, .mSources = sources }))
                    << misfit.mName;

                ContentStore::Store store(
                    TestingOpenMW::currentTestDirPath() / ("store" + std::to_string(at)), sContentStorePolicy);
                store.write(digest.getKey(), storeKindOf(ContentPassId::Shape), ShapePass::sVersion, payload,
                    ContentStore::Reach::Disk);
                ContentPreprocessor content(&store, ContentStore::Reach::Memory);
                std::vector<std::uint32_t> run;
                foldCard(content, run);
                EXPECT_EQ(run, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 })) << misfit.mName;
                foldCard(content, run);
                EXPECT_EQ(run, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 })) << misfit.mName;
                const PassStats stats = content.takeStats().at(ContentPassId::Shape);
                EXPECT_EQ(stats.mAsked, 2u) << misfit.mName;
                EXPECT_EQ(stats.mHits, 1u) << misfit.mName << ": the second, from the run's output";
            }

            // And the card's own output fits it: every corner and every normal its run leaves.
            std::vector<std::uint32_t> kept{ 0, 1, 2, 0, 2, 3 };
            std::vector<osg::Vec3f> normals(8);
            std::vector<std::uint32_t> sources;
            EXPECT_TRUE(
                ShapePass::fits(input, ShapePass::Output{ .mKept = kept, .mNormals = normals, .mSources = sources }));
            normals.clear();
            EXPECT_TRUE(
                ShapePass::fits(input, ShapePass::Output{ .mKept = kept, .mNormals = normals, .mSources = sources }));
        }

        /// **A stored chain that reads whole and does not fit its file is run again and replaced**, as a
        /// shape is: an 8 by 8 BC1 file of one level makes its chain whole in BC7 to a texel, 64 + 16 +
        /// 16 + 16 = 112 bytes. A form's byte alone, 111 bytes, 113 bytes, coarser levels of a file of
        /// one level, and nothing standing with a byte held are each a miss, and the run's 112 bytes are
        /// filed in the misfit's place. Coarser levels of an 8 by 8 file of two levels are its 4 by 4
        /// level, 8 bytes, and a painted one's own chain both levels, 32 + 8 = 40; a loose file has no
        /// chain but BC7's.
        TEST(RtxContentPreprocessorTest, aStoredChainThatDoesNotFitItsFileIsRunAgain)
        {
            const osg::ref_ptr<osg::Image> single = makeBc1(8, 8, 1);
            const ChainPass::Input input{ .mImage = *single, .mEncoding = TextureEncoding::Colour };
            ContentStore::Digest digest(sContentPasses.name(ContentPassId::Chain), ChainPass::sVersion);
            {
                ChainBuild build;
                ChainPass pass(build);
                pass.digest(input, digest);
                pass.release();
            }

            struct Misfit
            {
                std::string_view mName;
                ChainForm mForm;
                std::size_t mBytes;
            };
            const std::vector<Misfit> misfits{
                { "a form's byte alone", ChainForm::Bc7, 0 },
                { "a chain a byte short", ChainForm::Bc7, 111 },
                { "a chain a byte long", ChainForm::Bc7, 113 },
                { "coarser levels of a file of one level", ChainForm::Coarser, 8 },
                { "nothing standing with a byte held", ChainForm::Stands, 1 },
            };
            for (std::size_t at = 0; at < misfits.size(); ++at)
            {
                const Misfit& misfit = misfits[at];
                const MadeChain stored{ .mForm = misfit.mForm, .mBytes = Misc::ByteBuffer(misfit.mBytes) };
                EXPECT_FALSE(ChainPass::fits(input, stored)) << misfit.mName;

                Misc::ByteBuffer payload;
                ContentCodec<MadeChain>::encode(stored, payload);
                ContentStore::Store store(
                    TestingOpenMW::currentTestDirPath() / ("store" + std::to_string(at)), sContentStorePolicy);
                store.write(digest.getKey(), storeKindOf(ContentPassId::Chain), ChainPass::sVersion, payload,
                    ContentStore::Reach::Disk);
                ContentPreprocessor content(&store, ContentStore::Reach::Disk);
                for (int ask = 0; ask < 2; ++ask)
                {
                    MadeChain made;
                    content.chain(input, made);
                    EXPECT_EQ(made.mForm, ChainForm::Bc7) << misfit.mName;
                    EXPECT_EQ(made.mBytes.size(), 112u) << misfit.mName;
                }
                store.flush();
                const PassStats stats = content.takeStats().at(ContentPassId::Chain);
                EXPECT_EQ(stats.mAsked, 2u) << misfit.mName;
                EXPECT_EQ(stats.mHits, 1u) << misfit.mName << ": the second, from the run's output";
            }

            EXPECT_TRUE(ChainPass::fits(input, MadeChain{ .mForm = ChainForm::Bc7, .mBytes = Misc::ByteBuffer(112) }));

            const osg::ref_ptr<osg::Image> pair = makeBc1(8, 8, 2);
            const ChainPass::Input data{ .mImage = *pair, .mEncoding = TextureEncoding::Data };
            EXPECT_TRUE(ChainPass::fits(data, MadeChain{ .mForm = ChainForm::Coarser, .mBytes = Misc::ByteBuffer(8) }));
            EXPECT_FALSE(
                ChainPass::fits(data, MadeChain{ .mForm = ChainForm::Coarser, .mBytes = Misc::ByteBuffer(40) }));
            const ChainPass::Input painted{ .mImage = *pair, .mEncoding = TextureEncoding::Painted };
            EXPECT_TRUE(ChainPass::fits(painted, MadeChain{ .mForm = ChainForm::Own, .mBytes = Misc::ByteBuffer(40) }));
            EXPECT_FALSE(ChainPass::fits(painted, MadeChain{ .mForm = ChainForm::Own, .mBytes = Misc::ByteBuffer(8) }));

            const osg::ref_ptr<osg::Image> loose = makeLoose(8, 8);
            const ChainPass::Input looseInput{ .mImage = *loose, .mEncoding = TextureEncoding::Colour };
            EXPECT_FALSE(ChainPass::madeLayout(looseInput, ChainForm::Own).has_value());
            EXPECT_FALSE(ChainPass::madeLayout(looseInput, ChainForm::Coarser).has_value());
            EXPECT_FALSE(ChainPass::madeLayout(looseInput, ChainForm::Stands).has_value());
            EXPECT_TRUE(ChainPass::madeLayout(looseInput, ChainForm::Bc7).has_value());

            // A conversion makes its whole chain to a texel whatever its file carries: 8 by 8 to a
            // texel in BC7, 112 bytes, or a base colour in its BC1, 32 + 8 + 8 + 8 = 56; a
            // metal-roughness map has no form but BC7's.
            const ConversionPass::Input colour{
                .mImage = *pair, .mPartner = *pair, .mEncoding = TextureEncoding::ConvertedColour
            };
            EXPECT_TRUE(
                ConversionPass::fits(colour, MadeChain{ .mForm = ChainForm::Bc7, .mBytes = Misc::ByteBuffer(112) }));
            EXPECT_TRUE(
                ConversionPass::fits(colour, MadeChain{ .mForm = ChainForm::Own, .mBytes = Misc::ByteBuffer(56) }));
            EXPECT_FALSE(
                ConversionPass::fits(colour, MadeChain{ .mForm = ChainForm::Own, .mBytes = Misc::ByteBuffer(40) }));
            const ConversionPass::Input metal{
                .mImage = *pair, .mPartner = *pair, .mEncoding = TextureEncoding::ConvertedSpecular
            };
            EXPECT_FALSE(ConversionPass::madeLayout(metal, ChainForm::Own).has_value());
            EXPECT_FALSE(ConversionPass::madeLayout(metal, ChainForm::Coarser).has_value());
        }

        /// **A kept shape is handed back whole**: the pass hands over its normals as their words read back
        /// (`ShapePass::run`), which the codec keeps exactly, so a run and a later hit hand the mesh table one
        /// output. (0.5, 0.25, −1)'s word read back is unit and reads back to itself.
        TEST(RtxContentCacheTest, aKeptShapeIsHandedBackWhole)
        {
            ContentStore::Store store(TestingOpenMW::currentTestDirPath() / "store", sContentStorePolicy);
            ContentCache cache(&store, ContentStore::Reach::Disk);
            const ShapePass::Input input{ .mPositions = sCard, .mTriangles = sDoubled, .mSplits = true };
            const ContentStore::Key key{ .mHash = { 1, 2 } };

            const osg::Vec3f rounded = Shaders::unpackNormal(Shaders::packNormal(osg::Vec3f(0.5f, 0.25f, -1.0f)));
            EXPECT_NEAR(rounded.length(), 1.0f, 1e-6f);
            std::vector<std::uint32_t> kept{ 0, 1, 2 };
            std::vector<osg::Vec3f> normals(8, rounded);
            std::vector<std::uint32_t> sources;
            const ShapePass::Output output{ .mKept = kept, .mNormals = normals, .mSources = sources };
            cache.keep<ShapePass>(key, output);

            std::vector<std::uint32_t> foundKept;
            std::vector<osg::Vec3f> foundNormals;
            std::vector<std::uint32_t> foundSources;
            ShapePass::Output found{ .mKept = foundKept, .mNormals = foundNormals, .mSources = foundSources };
            ASSERT_TRUE(cache.find<ShapePass>(key, input, found));
            EXPECT_EQ(foundNormals, normals);
            EXPECT_EQ(foundKept, kept);
        }

        /// A sum is pass by pass and figure by figure, and the whole is every key, every found output's
        /// read and every run: 1 + 0.5 + 2 + 3 + 0.25 + 4 = 10.75 ms.
        TEST(RtxContentStatsTest, aSumIsPassByPassAndTheWholeIsEveryKeyAndRun)
        {
            ContentStats first;
            first.at(ContentPassId::Shape)
                = PassStats{ .mAsked = 2, .mHits = 1, .mKeyMs = 1.0, .mFoundMs = 0.5, .mRunMs = 2.0, .mKeyBytes = 10 };

            ContentStats second;
            second.at(ContentPassId::Shape) = PassStats{ .mAsked = 1, .mKeyMs = 3.0, .mFoundMs = 0.25, .mKeyBytes = 5 };
            second.at(ContentPassId::TexelMean) = PassStats{ .mAsked = 4, .mRunMs = 4.0 };

            Preprocessed both{ .mOnFrame = first };
            both += Preprocessed{ .mOnFrame = second, .mOffFrame = second, .mPrepared = first, .mLoading = second };

            const PassStats& fold = both.mOnFrame.at(ContentPassId::Shape);
            EXPECT_EQ(fold.mAsked, 3u);
            EXPECT_EQ(fold.mHits, 1u);
            EXPECT_EQ(fold.mKeyMs, 4.0);
            EXPECT_EQ(fold.mFoundMs, 0.75);
            EXPECT_EQ(fold.mRunMs, 2.0);
            EXPECT_EQ(fold.mKeyBytes, 15u);
            EXPECT_EQ(both.mOnFrame.at(ContentPassId::TexelMean).mAsked, 4u);
            EXPECT_EQ(both.mOnFrame.getMs(), 10.75);
            EXPECT_EQ(both.mOffFrame.getMs(), 7.25);
            EXPECT_EQ(both.mPrepared.getMs(), 3.5) << "the chain threads' figures not summed";
            EXPECT_EQ(both.mLoading.getMs(), 7.25) << "the loading screen's figures not summed";
        }
    }
}
