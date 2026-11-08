#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC formats, which Windows's and Apple's gl.h lack.
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentcodec.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>

namespace Rtx
{
    namespace
    {
        /// What a stored pass computed, as its codec files it, hashed: one output's word.
        template <class Output>
        std::uint64_t digestOf(const Output& output)
        {
            std::vector<std::byte> payload;
            ContentCodec<Output>::encode(output, payload);
            ContentDigest digest("guard", 0);
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
            auto* data = new unsigned char[bytes.size() + second.size()];
            std::memcpy(data, bytes.data(), bytes.size());
            if (!second.empty())
                std::memcpy(data + bytes.size(), second.data(), second.size());
            image->setImage(2, 2, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            if (!second.empty())
                image->setMipmapLevels(osg::Image::MipmapDataType{ static_cast<unsigned int>(bytes.size()) });
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
            auto* data = new unsigned char[sizeof(bytes)];
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
        /// output to a new build (`.notes/content-cache.md`, "A pass's version is held by a test").
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
            const MeanTexel mean = content.meanTexel(*painted);
            const AlphaRange span = content.alphaRange(*painted);

            const std::array<std::uint8_t, 16> heights{ 128, 128, 255, 0, 128, 136, 255, 0, 120, 128, 247, 100, 112,
                128, 251, 200 };
            MadeChain chain;
            content.chain(*makeImage(heights, { 0, 0, 0, 0 }), TextureEncoding::Normal, chain);
            ASSERT_EQ(chain.mForm, ChainForm::Bc7);
            MadeChain coarser;
            content.chain(*makeBlockImage(), TextureEncoding::Normal, coarser);
            ASSERT_EQ(coarser.mForm, ChainForm::Coarser);

            struct Recorded
            {
                const char* mPass;
                std::uint32_t mVersion;
                std::uint64_t mDigest;
            };
            const std::array computed{
                Recorded{ "shape", ShapePass::sVersion, digestOf(shape) },
                Recorded{ "texel mean", TexelMean::sVersion, digestOf(mean) },
                Recorded{ "alpha span", AlphaSpan::sVersion, digestOf(span) },
                Recorded{ "chain in BC7", ChainPass::sVersion, digestOf(chain) },
                Recorded{ "coarser chain", ChainPass::sVersion, digestOf(coarser) },
            };
            const std::array recorded{
                Recorded{ "shape", 3, 0xea9852832b8ceb62 },
                Recorded{ "texel mean", 1, 0xfaa7e2c796ee8c55 },
                Recorded{ "alpha span", 1, 0xb02496efa84a6d00 },
                Recorded{ "chain in BC7", 3, 0xce26916638ee39f4 },
                Recorded{ "coarser chain", 3, 0x5f247501ba043a44 },
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
