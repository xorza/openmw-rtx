#include <vector>

#include <gtest/gtest.h>

#include <osg/Image>

#include <components/rtx/error.hpp>
#include <components/rtx/texture.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

namespace RtxBridge
{
    namespace
    {
        /// One block's worth of image in `format`, which is all the description reads beyond it.
        osg::ref_ptr<osg::Image> makeBlock(GLenum format)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName("textures/tx_test.dds");
            image->allocateImage(4, 4, 1, format, GL_UNSIGNED_BYTE);
            return image;
        }

        /// DXT1 arrives under two names and both of them read the alpha bit.
        ///
        /// Almost none of Morrowind's DDS files set `DDPF_ALPHAPIXELS`, so OSG hands over its
        /// foliage as `GL_COMPRESSED_RGB_S3TC_DXT1_EXT`; taking that at its word decodes a canopy's
        /// punch-through blocks as opaque black and leaves every tree the card it was painted on.
        /// The bytes are identical either way — the format only decides whether the bit is looked at.
        TEST(RtxTextureBuilderTest, bothSpellingsOfDxt1ReadTheAlphaBit)
        {
            std::vector<Rtx::MipLevel> levels;

            EXPECT_EQ(describeImage(*makeBlock(GL_COMPRESSED_RGB_S3TC_DXT1_EXT), levels).mFormat,
                VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
            EXPECT_EQ(describeImage(*makeBlock(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT), levels).mFormat,
                VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
        }

        /// The formats Morrowind actually ships, kept apart. DXT3 carrying its own alpha is what
        /// the handful of soft-edged masks in the game are stored as.
        TEST(RtxTextureBuilderTest, theOtherBlockFormatsKeepTheirOwnMapping)
        {
            std::vector<Rtx::MipLevel> levels;

            EXPECT_EQ(
                describeImage(*makeBlock(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT), levels).mFormat, VK_FORMAT_BC2_SRGB_BLOCK);
            EXPECT_EQ(
                describeImage(*makeBlock(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT), levels).mFormat, VK_FORMAT_BC3_SRGB_BLOCK);
        }

        /// A format this cannot upload fails by name rather than uploading noise.
        TEST(RtxTextureBuilderTest, anUncompressedImageIsRefusedAndSaysWhich)
        {
            std::vector<Rtx::MipLevel> levels;
            EXPECT_THROW(describeImage(*makeBlock(GL_RGBA), levels), Rtx::Error);
        }
    }
}
