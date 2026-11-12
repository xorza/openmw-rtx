#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/rtxtool/instruments/contactsheet.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/vfs/pathutil.hpp>

namespace RtxTool
{
    namespace
    {
        /// The sheet has to show what the chain makes, or it is a picture of a different renderer.
        ///
        /// **Display-encoded, deliberately**: a painted colour's bytes are display-encoded, so dividing in
        /// linear and encoding again lands on a byte the chain would also produce, which makes this a
        /// cross-check. `Rtx::Testing::paintTwoTones` is 255 across its middle half under an estimate of
        /// 1.501: 0.66624 in light, 213 of 255. The same texture bound as a colour is the same picture in
        /// both halves, which makes the sheet an A/B.
        TEST(RtxContactSheetTest, theSheetAppliesTheCorrectionTheChainApplies)
        {
            Rtx::Testing::TestTexture painted = Rtx::Testing::paintTwoTones(32, 96);
            const Rtx::Testing::TestTexture colour = Rtx::Testing::paintTwoTones(32, 96);
            painted.mData.mEncoding = Rtx::TextureEncoding::Painted;

            const auto shownAt = [&](const Rtx::TextureData& texture, bool right) {
                const ContactSheet sheet = drawContactSheet(std::span(&texture, 1));
                EXPECT_EQ(sheet.mCount, 1u);

                const std::uint32_t x
                    = sheet.getLeftOf(0) + (right ? ContactSheet::getStride() : 0) + ContactSheet::getThumbnail() / 2;
                const std::uint32_t y = sheet.getTopOf(0) + ContactSheet::getThumbnail() / 2;
                return static_cast<int>(sheet.mPixels[(std::size_t{ y } * sheet.mWidth + x) * 4]);
            };

            EXPECT_EQ(shownAt(painted.mData, false), 255) << "the left half is the texture as it was drawn";
            EXPECT_NEAR(shownAt(painted.mData, true), 213, 1) << "and the right is it de-lit";
            EXPECT_EQ(shownAt(colour.mData, true), 255) << "a colour is not delit";
        }

        /// **The legend names what each pair shows, by its slot.** A table whose middle slot was
        /// freed describes two textures, slots 0 and 2, and the sheet draws them as pairs 0 and 1:
        /// the legend reads the first file and the third, where one counted along the table named
        /// the freed slot's empty path second. A bake is named by what it was made from.
        TEST(RtxContactSheetTest, theLegendNamesEachPairBySlot)
        {
            std::array<Rtx::TextureRow, 5> rows{};
            rows[0].mPath = VFS::Path::Normalized("textures/first.dds");
            rows[2].mPath = VFS::Path::Normalized("textures/third.dds");
            rows[3].mKind = Rtx::TextureKind::SpriteLight;
            rows[3].mPath = VFS::Path::Normalized("textures/first.dds");
            rows[4].mKind = Rtx::TextureKind::GroundGloss;
            rows[4].mGroundOf = 7;

            std::array<Rtx::TextureData, 4> drawn{};
            drawn[0].mSlot = 0;
            drawn[1].mSlot = 2;
            drawn[2].mSlot = 3;
            drawn[3].mSlot = 4;

            std::vector<std::string> names;
            listSheetNames(drawn, rows, names);
            EXPECT_EQ(names,
                (std::vector<std::string>{ "textures/first.dds", "textures/third.dds",
                    "the light of textures/first.dds", "the ground gloss of material 7" }));
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

            const ContactSheet sheet = drawContactSheet(textures);
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
            const ContactSheet sheet = drawContactSheet({});

            EXPECT_EQ(sheet.mCount, 0u);
            EXPECT_TRUE(sheet.mPixels.empty());
        }
    }
}
