#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/contactsheet.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/vfs/pathutil.hpp>

#include "../rtx/support/testtexture.hpp"

namespace RtxTool
{
    namespace
    {
        /// The sheet has to show what the frame does, or it is a picture of a different renderer.
        ///
        /// **Display-encoded, deliberately.** A content texture's bytes are display-encoded, so
        /// dividing in linear and encoding again lands on a byte the shader would also produce —
        /// which is what makes this a cross-check rather than two independent claims. The linear
        /// test format would not: there the file holds the light itself, and the sheet's byte and
        /// the frame's byte are answers to different questions.
        ///
        /// `Rtx::Testing::paintTwoTones` says what the texture is: 255 across its middle half under an
        /// estimate of 1.501, which is 0.66624 in light and encodes to 213 of 255. The pixel test
        /// in the renderer, `aTexturesPaintedLightIsDividedBackOutOfItsAlbedo`, works the same
        /// arithmetic for the same reason.
        TEST(RtxContactSheetTest, theSheetAppliesTheCorrectionTheFrameApplies)
        {
            const Rtx::Testing::TestTexture painted = Rtx::Testing::paintTwoTones(32, 96);

            const auto shownAt = [&](float strength, bool right) {
                const ContactSheet sheet = drawContactSheet(std::span(&painted.mData, 1), strength);
                EXPECT_EQ(sheet.mCount, 1u);

                const std::uint32_t x
                    = sheet.getLeftOf(0) + (right ? ContactSheet::getStride() : 0) + ContactSheet::getThumbnail() / 2;
                const std::uint32_t y = sheet.getTopOf(0) + ContactSheet::getThumbnail() / 2;
                return static_cast<int>(sheet.mPixels[(std::size_t{ y } * sheet.mWidth + x) * 4]);
            };

            EXPECT_EQ(shownAt(1.0f, false), 255) << "the left half is the texture as it was drawn";
            EXPECT_NEAR(shownAt(1.0f, true), 213, 1) << "and the right is it de-lit";

            // At no strength the two halves are the same picture, which is what makes the sheet an
            // A/B rather than a claim.
            EXPECT_EQ(shownAt(0.0f, true), 255);
        }

        /// **The legend names what each pair shows, by its slot.** A table whose middle slot was
        /// freed describes two textures, slots 0 and 2, and the sheet draws them as pairs 0 and 1:
        /// the legend reads the first file and the third, where one counted along the table named
        /// the freed slot's empty path second. A bake is named by its key.
        TEST(RtxContactSheetTest, theLegendNamesEachPairBySlot)
        {
            std::array<Rtx::TextureRow, 4> rows{};
            rows[0].mPath = VFS::Path::Normalized("textures/first.dds");
            rows[2].mPath = VFS::Path::Normalized("textures/third.dds");
            rows[3].mKind = Rtx::TextureKind::Baked;
            rows[3].mBaked = "bake:textures/first.dds";

            std::array<Rtx::TextureData, 3> drawn{};
            drawn[0].mSlot = 0;
            drawn[1].mSlot = 2;
            drawn[2].mSlot = 3;

            std::vector<std::string_view> names;
            listSheetNames(drawn, rows, names);
            EXPECT_EQ(names,
                (std::vector<std::string_view>{
                    "textures/first.dds", "textures/third.dds", "bake:textures/first.dds" }));
        }

        /// **What has no colour to read is a checker of eight-texel squares, 32 and 96, in both
        /// halves**, and the gap between the halves stays the paper's 64. A bake is shaped like its
        /// source and carries no bytes, and a BC5 map's bytes are two data channels: bytes of 255
        /// read as colour would draw 255.
        TEST(RtxContactSheetTest, whatHasNoColourToReadIsDrawnAsAChecker)
        {
            std::array<Rtx::TextureData, 3> textures{};
            textures[0].mSource = Rtx::TextureSource::SpriteBake;
            textures[0].mWidth = 64;
            textures[0].mHeight = 64;

            const std::array<std::byte, 16> block = [] {
                std::array<std::byte, 16> bytes{};
                bytes.fill(std::byte{ 255 });
                return bytes;
            }();
            const std::array<Rtx::MipLevel, 1> level{ Rtx::MipLevel{ .mOffset = 0, .mWidth = 4, .mHeight = 4 } };
            textures[1].mFormat = Rtx::TextureFormat::Bc5Unorm;
            textures[1].mEncoding = Rtx::TextureEncoding::Normal;
            textures[1].mWidth = 4;
            textures[1].mHeight = 4;
            textures[1].mBytes = block;
            textures[1].mLevels = level;

            const Rtx::Testing::TestTexture painted = Rtx::Testing::paintTwoTones(32, 96);
            textures[2] = painted.mData;

            EXPECT_FALSE(Rtx::readsColour(textures[0]));
            EXPECT_FALSE(Rtx::readsColour(textures[1]));
            EXPECT_TRUE(Rtx::readsColour(textures[2]));

            const ContactSheet sheet = drawContactSheet(textures, 0.0f);
            const auto shownAt = [&](std::uint32_t index, std::uint32_t x, std::uint32_t y) {
                const std::size_t at
                    = (std::size_t{ sheet.getTopOf(index) + y } * sheet.mWidth + sheet.getLeftOf(index) + x) * 4;
                return static_cast<int>(sheet.mPixels[at]);
            };

            for (const std::uint32_t index : { 0u, 1u })
                for (const std::uint32_t half : { 0u, ContactSheet::getStride() })
                {
                    EXPECT_EQ(shownAt(index, half, 0), 32) << index;
                    EXPECT_EQ(shownAt(index, half + 7, 7), 32) << index;
                    EXPECT_EQ(shownAt(index, half + 8, 0), 96) << index;
                    EXPECT_EQ(shownAt(index, half, 8), 96) << index;
                    EXPECT_EQ(shownAt(index, half + 8, 8), 32) << index;
                }
            EXPECT_EQ(shownAt(0, ContactSheet::getThumbnail(), 0), 64) << "the gap is the paper";

            const std::uint32_t middle = ContactSheet::getThumbnail() / 2;
            EXPECT_EQ(shownAt(2, middle, middle), 255) << "a readable texture beside them is still drawn";
        }

        /// A cell that used no textures has no sheet to draw, and says so rather than writing one.
        TEST(RtxContactSheetTest, nothingToShowDrawsNothing)
        {
            const ContactSheet sheet = drawContactSheet({}, 1.0f);

            EXPECT_EQ(sheet.mCount, 0u);
            EXPECT_TRUE(sheet.mPixels.empty());
        }
    }
}
