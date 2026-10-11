#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Vec4f>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/countingrenderer.hpp>
#include <components/myguirtx/sharedtexture.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/slot.hpp>

namespace MyGUIRtx
{
    namespace
    {
        /// A packed RGBA picture whose bytes count up from `first`, so every byte sent says which
        /// picture and which place it came from.
        osg::ref_ptr<osg::Image> makePicture(const int width, const int height, const std::uint8_t first)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            for (unsigned int at = 0; at < image->getTotalSizeInBytes(); ++at)
                image->data()[at] = static_cast<std::uint8_t>(first + at);
            return image;
        }

        std::vector<std::uint8_t> bytesOf(const osg::Image& image)
        {
            return std::vector<std::uint8_t>(image.data(), image.data() + image.getTotalSizeInBytes());
        }

        std::array<std::uint32_t, 4> cornersOf(const Rtx::GuiRegion& region)
        {
            return { region.mX, region.mY, region.mWidth, region.mHeight };
        }

        /// **Whole, whenever the picture moves, and nothing when it does not.** A row written in
        /// place goes as the whole picture and not as that row, which is what says no comparison
        /// against what was sent is left; another picture of the same shape keeps the slot, and one
        /// of another shape takes a new one.
        TEST(RtxSharedTextureTest, aMirrorSendsItsPictureWholeWheneverThePictureMoves)
        {
            Rtx::Testing::CountingRenderer renderer;
            const osg::ref_ptr<osg::Image> first = makePicture(4, 3, 0);
            const osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(first);

            SharedTexture mirror(renderer, *texture);
            ASSERT_EQ(renderer.mLent.size(), 1u);
            EXPECT_EQ(cornersOf(renderer.mLent[0]), (std::array<std::uint32_t, 4>{ 0, 0, 4, 3 }));
            EXPECT_EQ(renderer.mLending, bytesOf(*first));
            EXPECT_EQ(renderer.mGuiSent, 1u);
            const Rtx::GuiSlot slot = mirror.getSlot();

            mirror.refresh();
            EXPECT_EQ(renderer.mLent.size(), 1u);
            EXPECT_EQ(renderer.mGuiSent, 1u);

            // The second of three rows, 16 bytes in: what a comparison would have sent alone.
            for (int x = 0; x < 16; ++x)
                first->data(0, 1)[x] = 200;
            first->dirty();
            mirror.refresh();
            ASSERT_EQ(renderer.mLent.size(), 2u);
            EXPECT_EQ(cornersOf(renderer.mLent[1]), (std::array<std::uint32_t, 4>{ 0, 0, 4, 3 }));
            EXPECT_EQ(renderer.mLending, bytesOf(*first));
            EXPECT_EQ(renderer.mGuiSent, 2u);

            const osg::ref_ptr<osg::Image> second = makePicture(4, 3, 100);
            texture->setImage(second);
            mirror.refresh();
            ASSERT_EQ(renderer.mLent.size(), 3u);
            EXPECT_EQ(cornersOf(renderer.mLent[2]), (std::array<std::uint32_t, 4>{ 0, 0, 4, 3 }));
            EXPECT_EQ(renderer.mLending, bytesOf(*second));
            EXPECT_EQ(mirror.getSlot(), slot);

            const osg::ref_ptr<osg::Image> smaller = makePicture(2, 2, 50);
            texture->setImage(smaller);
            mirror.refresh();
            ASSERT_EQ(renderer.mLent.size(), 4u);
            EXPECT_EQ(cornersOf(renderer.mLent[3]), (std::array<std::uint32_t, 4>{ 0, 0, 2, 2 }));
            EXPECT_EQ(renderer.mLending, bytesOf(*smaller));
            EXPECT_NE(mirror.getSlot(), slot);
            EXPECT_EQ(renderer.mGuiSent, 4u);

            // Three channels, as a save's thumbnail is: each byte read as n / 255 and written back
            // as n / 255 * 255 + 0.5 rounded down, which is n, beside an opaque alpha.
            const osg::ref_ptr<osg::Image> rgb = new osg::Image;
            rgb->allocateImage(2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
            for (unsigned int at = 0; at < 6; ++at)
                rgb->data()[at] = static_cast<std::uint8_t>(10 * (at + 1));
            texture->setImage(rgb);
            mirror.refresh();
            EXPECT_EQ(renderer.mLending, (std::vector<std::uint8_t>{ 10, 20, 30, 255, 40, 50, 60, 255 }));
        }

        /// **The picture last sent lives until the mirror has seen the next one.** One the game let
        /// go of could otherwise be followed by another at its address with the same modified count,
        /// and the mirror would take the new picture for the one it already sent.
        TEST(RtxSharedTextureTest, aMirrorHoldsThePictureItLastSentUntilItSeesTheNext)
        {
            Rtx::Testing::CountingRenderer renderer;
            osg::ref_ptr<osg::Image> first = makePicture(4, 3, 0);
            const osg::observer_ptr<osg::Image> watched(first);
            const osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(first);
            SharedTexture mirror(renderer, *texture);

            texture->setImage(makePicture(4, 3, 100));
            first = nullptr;
            EXPECT_TRUE(watched.valid());

            mirror.refresh();
            EXPECT_FALSE(watched.valid());
        }

        /// **A picture is sent as `osg::Image::getColor` reads it, in every format the game hands
        /// one in**, byte by byte where the format is bytes: RGBA and BGRA as they stand, RGB and BGR
        /// opaque, a luminance three times with its alpha or opaque. Three pixels by two in each, so
        /// a row of a three-byte format is padded to four bytes, which the copy steps over. A float
        /// picture, which is no bytes, goes through `getColor` itself.
        TEST(RtxSharedTextureTest, aPictureIsSentAsItsColoursReadInEveryFormat)
        {
            constexpr int width = 3;
            constexpr int height = 2;
            const std::array formats{ GL_RGBA, GL_BGRA, GL_RGB, GL_BGR, GL_LUMINANCE, GL_LUMINANCE_ALPHA };
            for (const GLenum format : formats)
            {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->allocateImage(width, height, 1, format, GL_UNSIGNED_BYTE, 4);
                for (unsigned int at = 0; at < image->getTotalSizeInBytes(); ++at)
                    image->data()[at] = static_cast<std::uint8_t>(17 + 37 * at);

                std::vector<std::uint8_t> read;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const osg::Vec4f colour = image->getColor(x, y);
                        for (int channel = 0; channel < 4; ++channel)
                            read.push_back(static_cast<std::uint8_t>(colour[channel] * 255.0f + 0.5f));
                    }

                Rtx::Testing::CountingRenderer renderer;
                const osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image);
                const SharedTexture mirror(renderer, *texture);
                EXPECT_EQ(renderer.mLending, read) << "format " << format;
            }

            osg::ref_ptr<osg::Image> floats = new osg::Image;
            floats->allocateImage(width, height, 1, GL_RGBA, GL_FLOAT);
            auto* values = reinterpret_cast<float*>(floats->data());
            for (int at = 0; at < width * height * 4; ++at)
                values[at] = static_cast<float>(at) / 23.0f;
            Rtx::Testing::CountingRenderer renderer;
            const osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(floats);
            const SharedTexture mirror(renderer, *texture);
            ASSERT_EQ(renderer.mLending.size(), std::size_t{ width * height * 4 });
            EXPECT_EQ(renderer.mLending[4], static_cast<std::uint8_t>(4.0f / 23.0f * 255.0f + 0.5f));

            // **What `getColor` cannot read, read as GL samples it**: an X8 file's spare byte is
            // no alpha, and an old mod's sixteen-bit words are their channels, where `getColor`
            // answers white for every one. Full red in each: R5G6B5 `0xF800`, A1R5G5B5 `0x7C00`
            // with its alpha bit clear, X1R5G5B5 the same word with no alpha at all, and A4R4G4B4
            // `0x0F00` with its alpha nibble nought.
            struct Packed
            {
                GLenum mFormat;
                GLenum mType;
                GLenum mInternal;
                std::uint16_t mWord;
                std::array<std::uint8_t, 4> mSent;
            };
            for (const Packed& one : {
                     Packed{ GL_RGB, GL_UNSIGNED_SHORT_5_6_5, GL_RGB, 0xF800, { 255, 0, 0, 255 } },
                     Packed{ GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, GL_RGBA, 0x7C00, { 255, 0, 0, 0 } },
                     Packed{ GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, GL_RGB, 0x7C00, { 255, 0, 0, 255 } },
                     Packed{ GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, GL_RGBA, 0x0F00, { 255, 0, 0, 0 } },
                 })
            {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                image->allocateImage(1, 1, 1, one.mFormat, one.mType);
                image->setInternalTextureFormat(static_cast<GLint>(one.mInternal));
                std::memcpy(image->data(), &one.mWord, sizeof(one.mWord));

                Rtx::Testing::CountingRenderer counted;
                const osg::ref_ptr<osg::Texture2D> sixteen = new osg::Texture2D(image);
                const SharedTexture sent(counted, *sixteen);
                EXPECT_EQ(counted.mLending, std::vector<std::uint8_t>(one.mSent.begin(), one.mSent.end()))
                    << "type " << one.mType << ", internal " << one.mInternal;
            }

            osg::ref_ptr<osg::Image> spare = new osg::Image;
            spare->allocateImage(1, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE);
            spare->setInternalTextureFormat(GL_RGB);
            const std::array<std::uint8_t, 4> blueGreenRedNought{ 30, 20, 10, 0 };
            std::memcpy(spare->data(), blueGreenRedNought.data(), blueGreenRedNought.size());
            Rtx::Testing::CountingRenderer counted;
            const osg::ref_ptr<osg::Texture2D> x8 = new osg::Texture2D(spare);
            const SharedTexture sent(counted, *x8);
            EXPECT_EQ(counted.mLending, (std::vector<std::uint8_t>{ 10, 20, 30, 255 }))
                << "the spare byte read as alpha";
        }
    }
}
