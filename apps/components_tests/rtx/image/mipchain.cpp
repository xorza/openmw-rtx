#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/mipchain.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>

namespace Rtx
{
    namespace
    {
        using Testing::TestTexture;

        /// Adds one level, painted from `bytes` or left blank where none are given.
        ///
        /// A blank one is for a test that only wants the file to claim the level: nothing reads its
        /// texels, because a chain that is already there is never filtered.
        void addLevel(TestTexture& texture, std::uint32_t width, std::uint32_t height,
            std::initializer_list<std::uint8_t> bytes = {})
        {
            texture.mLevels.push_back(MipLevel{ static_cast<std::uint32_t>(texture.mBytes.size()), width, height });

            if (bytes.size() > 0)
                texture.mBytes.insert(texture.mBytes.end(), bytes);
            else
                texture.mBytes.resize(texture.mBytes.size() + std::size_t{ width } * height * 4);
        }

        /// One channel of one texel of a built level.
        std::uint32_t channelAt(
            const TextureData& texture, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::size_t channel)
        {
            const MipLevel& which = texture.mLevels[level];

            return std::to_integer<std::uint32_t>(
                texture.mBytes[which.mOffset + (std::size_t{ y } * which.mWidth + x) * 4 + channel]);
        }

        /// A texture that carried its own chain is left exactly as it arrived.
        ///
        /// **The ordinary case, and it has to cost nothing.** Five thousand of Morrowind's textures
        /// ship a chain; a builder that rebuilt them would decode every block in the game on every
        /// cell load, and would upload them loose.
        TEST(RtxMipChainTest, aTextureThatCarriedItsOwnChainIsLeftAlone)
        {
            TestTexture whole;
            addLevel(whole, 4, 4);
            addLevel(whole, 2, 2);
            addLevel(whole, 1, 1);
            whole.describe(4, 4, "whole");

            EXPECT_TRUE(Testing::MipChain(whole.mData).isEmpty());

            // **And a chain that stops short is a chain.** Morrowind's own end at eight texels
            // rather than at one, and rebuilding those would decompress the whole game to gain a
            // level no ray can tell from the one above it.
            TestTexture partial;
            addLevel(partial, 4, 4);
            addLevel(partial, 2, 2);
            partial.describe(4, 4, "partial");

            EXPECT_TRUE(Testing::MipChain(partial.mData).isEmpty());

            // **And a single texel is a whole chain**, which is the one extent that needs no levels
            // under it.
            TestTexture one;
            addLevel(one, 1, 1, { 0, 0, 0, 255 });
            one.describe(1, 1, "one");

            EXPECT_TRUE(Testing::MipChain(one.mData).isEmpty());
        }

        /// Every level a file left out is the mean of the one above it.
        ///
        /// Four quads of one value apiece — 100, 200, 0 and 40 — so the second level is those four
        /// numbers and the third is their mean, `(100 + 200 + 0 + 40) / 4 = 85`. Opaque throughout,
        /// which is what makes the weighing below a separate question from this one.
        TEST(RtxMipChainTest, everyLevelAFileLeftOutIsTheMeanOfTheOneAboveIt)
        {
            TestTexture four;
            for (const std::uint8_t value : { 100, 200, 100, 200, 0, 40, 0, 40 })
                for (int twice = 0; twice < 2; ++twice)
                    for (const std::uint8_t byte : { value, value, value, std::uint8_t{ 255 } })
                        four.mBytes.push_back(byte);

            four.mLevels.push_back(MipLevel{ 0, 4, 4 });
            four.describe(4, 4, "four");

            const Testing::MipChain chain(four.mData);
            ASSERT_FALSE(chain.isEmpty());

            const TextureData built = chain.describe();
            ASSERT_EQ(built.mLevels.size(), 3u) << "4 by 4 runs down to one texel in three levels";
            EXPECT_EQ(built.mWidth, 4u);
            EXPECT_EQ(built.mHeight, 4u);
            EXPECT_EQ(built.mFormat, TextureFormat::Rgba8Unorm) << "what has no curve under it keeps none";

            // The finest level is the file's own, carried across rather than filtered.
            EXPECT_EQ(channelAt(built, 0, 0, 0, 0), 100u);
            EXPECT_EQ(channelAt(built, 0, 3, 3, 0), 40u);

            EXPECT_EQ(built.mLevels[1].mWidth, 2u);
            EXPECT_EQ(channelAt(built, 1, 0, 0, 0), 100u);
            EXPECT_EQ(channelAt(built, 1, 1, 0, 0), 200u);
            EXPECT_EQ(channelAt(built, 1, 0, 1, 0), 0u);
            EXPECT_EQ(channelAt(built, 1, 1, 1, 0), 40u);

            EXPECT_EQ(built.mLevels[2].mWidth, 1u);
            EXPECT_EQ(channelAt(built, 2, 0, 0, 0), 85u) << "the mean of the four quads";
            EXPECT_EQ(channelAt(built, 2, 0, 0, 3), 255u) << "nothing was transparent, so nothing faded";
        }

        /// A colour is weighed by the alpha carrying it, and the alpha is not.
        ///
        /// **A texel nothing painted has no colour**, and a punch-through block stores black where
        /// it painted nothing — so an even mean draws a dark rim round every leaf card and every
        /// raindrop one level down. Two white texels at full alpha beside two black ones at none
        /// come to white at half alpha, where an even mean would come to 127.
        TEST(RtxMipChainTest, aColourIsWeighedByTheAlphaCarryingItAndTheAlphaIsNot)
        {
            TestTexture pair;
            addLevel(pair, 2, 2, { 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0 });
            pair.describe(2, 2, "pair");

            const Testing::MipChain chain(pair.mData);
            ASSERT_FALSE(chain.isEmpty());

            const TextureData built = chain.describe();
            ASSERT_EQ(built.mLevels.size(), 2u);

            EXPECT_EQ(channelAt(built, 1, 0, 0, 0), 255u) << "the black the transparent texels stored was averaged in";
            EXPECT_EQ(channelAt(built, 1, 0, 0, 3), 128u) << "half of it was painted";

            // **And a quad with nothing painted in it still has a colour to state.** There is no
            // weight to divide by there, so the even mean is what is left.
            TestTexture empty;
            addLevel(empty, 2, 2, { 60, 60, 60, 0, 20, 20, 20, 0, 60, 60, 60, 0, 20, 20, 20, 0 });
            empty.describe(2, 2, "empty");

            const Testing::MipChain none(empty.mData);
            ASSERT_FALSE(none.isEmpty());
            EXPECT_EQ(channelAt(none.describe(), 1, 0, 0, 0), 40u);
            EXPECT_EQ(channelAt(none.describe(), 1, 0, 0, 3), 0u);

            // **Data's alpha is a channel like the others**, a height or a gloss, and its box is
            // even: the white and the black meet at 127.5, which rounds to 128.
            TestTexture data;
            addLevel(data, 2, 2, { 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0 });
            data.describe(2, 2, "data");
            data.mData.mEncoding = TextureEncoding::Data;

            const Testing::MipChain even(data.mData);
            ASSERT_FALSE(even.isEmpty());
            EXPECT_EQ(channelAt(even.describe(), 1, 0, 0, 0), 128u) << "data was weighed by its alpha";
            EXPECT_EQ(channelAt(even.describe(), 1, 0, 0, 3), 128u);

            // **And a chain says what it is read as**, its source's encoding, or a chain built from
            // data would be read as a colour.
            EXPECT_EQ(even.describe().mEncoding, TextureEncoding::Data);
            EXPECT_EQ(built.mEncoding, TextureEncoding::Colour);
        }

        /// **An odd extent is halved by the box of its own width**, three taps a texel, so its last
        /// texel is read. Five texels along a line, the last of them 250 and the rest nought: level
        /// one is two texels, `(2, 2, 1) / 5` over texels 0 to 2 and `(1, 2, 2) / 5` over 2 to 4,
        /// so nought and `250 * 2 / 5 = 100`; level two is their mean, 50. Halved as two, texel 4
        /// was never read and both levels were nought.
        TEST(RtxMipChainTest, anOddExtentIsHalvedByTheBoxOfItsOwnWidth)
        {
            TestTexture line;
            for (const std::uint8_t value : { 0, 0, 0, 0, 250 })
                for (const std::uint8_t byte : { value, value, value, std::uint8_t{ 255 } })
                    line.mBytes.push_back(byte);
            line.mLevels.push_back(MipLevel{ 0, 5, 1 });
            line.describe(5, 1, "line");

            const Testing::MipChain chain(line.mData);
            ASSERT_FALSE(chain.isEmpty());
            const TextureData built = chain.describe();
            ASSERT_EQ(built.mLevels.size(), 3u) << "five texels run down to one in three levels";
            ASSERT_EQ(built.mLevels[1].mWidth, 2u);

            EXPECT_EQ(channelAt(built, 1, 0, 0, 0), 0u);
            EXPECT_EQ(channelAt(built, 1, 1, 0, 0), 100u) << "the last texel of an odd extent was dropped";
            EXPECT_EQ(channelAt(built, 2, 0, 0, 0), 50u);
            EXPECT_EQ(channelAt(built, 2, 0, 0, 3), 255u) << "the weights did not sum to one";
        }

        /// A display-encoded texture is averaged in light and written back encoded.
        ///
        /// **The number `Rtx::toLinear` names.** Half of nothing and half of white meet at 188 in
        /// light and at 128 in bytes, and the second is every fade in the game coming out muddy.
        /// Which of the two this does is the whole difference between an sRGB format and the one
        /// above.
        TEST(RtxMipChainTest, aDisplayEncodedTextureIsAveragedInLight)
        {
            TestTexture pair;
            addLevel(pair, 2, 2, { 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255 });
            pair.describe(2, 2, "pair", TextureFormat::Rgba8Srgb);

            const Testing::MipChain chain(pair.mData);
            ASSERT_FALSE(chain.isEmpty());

            const TextureData built = chain.describe();
            EXPECT_EQ(built.mFormat, TextureFormat::Rgba8Srgb) << "what arrived encoded stays encoded";
            EXPECT_EQ(channelAt(built, 1, 0, 0, 0), 188u);
        }

        /// A chain built again is the texture it was handed and nothing of the one before, and it
        /// costs the heap nothing to say so.
        ///
        /// **What lets `SceneTextures` keep a pool of these.** A loader that describes a cell's
        /// worth builds a chain into the same object over and over; one that carried the last
        /// texture's levels through would upload one texture's chain under another's name, and one
        /// that gave its room back would take a megabyte and a half from the heap per chainless
        /// texture on the frame the cell lands.
        TEST(RtxMipChainTest, aChainBuiltAgainIsTheNewTextureAndKeepsTheRoomOfTheLast)
        {
            TestTexture single;
            addLevel(single, 4, 4);
            single.describe(4, 4, "single");

            TestTexture whole;
            addLevel(whole, 4, 4);
            addLevel(whole, 2, 2);
            addLevel(whole, 1, 1);
            whole.describe(4, 4, "whole");

            Testing::MipChain chain;
            chain.build(single.mData);
            ASSERT_FALSE(chain.isEmpty()) << "the texture this one has to stop carrying";
            ASSERT_EQ(chain.describe().mLevels.size(), std::size_t{ 3 });

            chain.build(whole.mData);
            EXPECT_TRUE(chain.isEmpty()) << "the last texture's levels came through";

            // **Back to the shape it already grew for**, which is the case the pool is made of: the
            // levels, the texels and the alpha the colours are weighed by are all still here.
            const std::size_t before = Testing::getAllocationCount();
            chain.build(single.mData);
            const std::size_t spent = Testing::getAllocationCount() - before;

            EXPECT_EQ(spent, 0u) << "a rebuild reached the heap " << spent << " times";
            EXPECT_FALSE(chain.isEmpty());
        }
    }
}
