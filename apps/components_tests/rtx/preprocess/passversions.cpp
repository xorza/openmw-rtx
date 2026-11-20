#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // IWYU pragma: keep: the S3TC formats, which Windows's gl.h lacks
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/pooledchain.hpp>
#include <components/contentstore/key.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/rtx/image/imagefacts.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentcodec.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
#include <components/rtx/preprocess/texture/filefactspass.hpp>

namespace Rtx
{
    namespace
    {
        /// What a stored pass computed, as its codec files it, hashed: one output's word.
        template <class Output>
        std::uint64_t digestOf(const Output& output)
        {
            Misc::ByteBuffer payload;
            ContentCodec<Output>::encode(output, payload);
            ContentStore::Digest digest("guard", 0);
            digest.add(std::span<const std::byte>(payload));
            return digest.getKey().mHash[0];
        }

        /// A unit cube whose corners average its three faces' normals, every face two triangles: what
        /// the split cuts at every edge. **Its cut normals are the faces' axes exactly**, a piece's two
        /// triangles being coplanar, so no system's `acos` moves the digest by its last bit.
        struct SmoothCube
        {
            std::array<osg::Vec3f, 8> mPositions;
            std::array<osg::Vec3f, 8> mNormals;
            std::array<std::uint32_t, 36> mTriangles{ 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1,
                6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7 };

            SmoothCube()
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

        /// A 2x2 RGBA8 image of `bytes`, carrying a second level of `second` where it is given.
        osg::ref_ptr<osg::Image> makeImage(
            const std::array<std::uint8_t, 16>& bytes, const std::vector<std::uint8_t>& second = {})
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* data = new std::uint8_t[bytes.size() + second.size()];
            std::memcpy(data, bytes.data(), bytes.size());
            if (!second.empty())
                std::memcpy(data + bytes.size(), second.data(), second.size());
            image->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            if (!second.empty())
                image->setMipmapLevels(osg::Image::MipmapDataType{ static_cast<std::uint32_t>(bytes.size()) });
            image->setFileName("textures/tx_guard.dds");
            return image;
        }

        /// A 4x4 BC1 image of two levels: its finest one block between (132, 130, 255) and black, its
        /// indices every one of the four, and its second level black.
        osg::ref_ptr<osg::Image> makeBlockImage()
        {
            const std::uint8_t bytes[] = { 0x1f, 0x84, 0x00, 0x00, 0x1b, 0x6c, 0xb1, 0xe4, //
                0, 0, 0, 0, 0, 0, 0, 0 };
            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* data = new std::uint8_t[sizeof(bytes)];
            std::memcpy(data, bytes, sizeof(bytes));
            image->setImage(4, 4, 1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE,
                data, osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(osg::Image::MipmapDataType{ 8 });
            image->setFileName("textures/tx_guard_block.dds");
            return image;
        }

        /// **Every stored pass's version is held to what it computes**: each runs over fixed inputs,
        /// and the digest of what its codec files is recorded beside its `sVersion`. An output that
        /// moves while the version stays fails here, so a forgotten bump cannot hand a store's old
        /// output to a new build (`docs/architecture.md`, "A pass's version is held by a test").
        /// **Where this fails, raise the pass's `sVersion` and record both again**: the store files
        /// every output of the new version apart from the old.
        TEST(RtxPassVersionsTest, everyStoredPassComputesWhatItsVersionRecords)
        {
            ContentPreprocessor content;

            const SmoothCube cube;
            std::vector<std::uint32_t> kept;
            std::vector<osg::Vec3f> normals;
            std::vector<std::uint32_t> sources;
            ShapePass::Output shape{ .mKept = kept, .mNormals = normals, .mSources = sources };
            content.shape(ShapePass::Input{ .mPositions = cube.mPositions,
                              .mNormals = cube.mNormals,
                              .mTriangles = cube.mTriangles,
                              .mSplits = true },
                shape);
            ASSERT_EQ(sources.size(), 16u) << "the cube was not split";

            const std::array<std::uint8_t, 16> paint{ 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255,
                255 };
            const osg::ref_ptr<osg::Image> painted = makeImage(paint);
            const ImageFacts facts = content.facts(*painted);
            ASSERT_TRUE(facts.mReachesSolid);

            const std::array<std::uint8_t, 16> heights{ 128, 128, 255, 0, 128, 136, 255, 0, 120, 128, 247, 100, 112,
                128, 251, 200 };
            const MadeChain chain = *Testing::chainOf(content,
                ChainPass::Input{
                    .mImage = *makeImage(heights, { 0, 0, 0, 0 }), .mEncoding = TextureEncoding::Normal });
            ASSERT_EQ(chain.mForm, ChainForm::Bc7);
            const MadeChain data = *Testing::chainOf(
                content, ChainPass::Input{ .mImage = *makeBlockImage(), .mEncoding = TextureEncoding::Data });
            ASSERT_EQ(data.mForm, ChainForm::Own);
            const ImageFacts heightFacts = content.facts(*makeImage(heights));
            const MadeChain converted = *Testing::chainOf(content,
                ConversionPass::Input{ .mImage = *painted,
                    .mPartner = *makeImage(heights),
                    .mEncoding = TextureEncoding::ConvertedSpecular });
            ASSERT_EQ(converted.mForm, ChainForm::Bc7);

            struct Recorded
            {
                const char* mPass;
                std::uint32_t mVersion;
                std::uint64_t mDigest;
            };
            const std::array computed{
                Recorded{ "shape", ShapePass::sVersion, digestOf(shape) },
                Recorded{ "file facts", FileFactsPass::sVersion, digestOf(facts) },
                Recorded{ "chain in BC7", ChainPass::sVersion, digestOf(chain) },
                Recorded{ "data chain", ChainPass::sVersion, digestOf(data) },
                Recorded{ "heights' facts", FileFactsPass::sVersion, digestOf(heightFacts) },
                Recorded{ "conversion", ConversionPass::sVersion, digestOf(converted) },
            };
            const std::array recorded{
                Recorded{ "shape", 4, 0xfe8b49f90aed1c6d },
                Recorded{ "file facts", 2, 0x9a30665f65b8cade },
                Recorded{ "chain in BC7", 8, 0x49e28ea38c0ba847 },
                Recorded{ "data chain", 8, 0x6bf8b7518c018d9e },
                Recorded{ "heights' facts", 2, 0x1b363cb9a6c14cbe },
                Recorded{ "conversion", 6, 0xed492221331d8e6e },
            };
            for (std::size_t at = 0; at < computed.size(); ++at)
            {
                EXPECT_EQ(computed[at].mVersion, recorded[at].mVersion)
                    << computed[at].mPass << ": a version raised without its digest recorded again";
                EXPECT_EQ(computed[at].mDigest, recorded[at].mDigest)
                    << computed[at].mPass << ": the output moved under version " << computed[at].mVersion << ", 0x"
                    << std::hex << computed[at].mDigest;
            }
        }
    }
}
