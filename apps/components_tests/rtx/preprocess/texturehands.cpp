#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/image.hpp>
#include <components/rtx/common/bufferpool.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/texturefeed.hpp>
#include <components/rtx/preprocess/texturehands.hpp>

namespace Rtx
{
    namespace
    {
        /// A request for `file` read as colour, from level `first` to its last, under `ticket`.
        FeedRequest requestOf(
            const osg::ref_ptr<const osg::Image>& file, const std::uint32_t ticket, const std::uint8_t first)
        {
            const ImageShape shape = shapeOfFile(*file, nullptr, TextureEncoding::Colour);
            return FeedRequest{ .mTicket = FeedTicket{ ticket },
                .mFile = file,
                .mEncoding = TextureEncoding::Colour,
                .mShape = shape,
                .mLevels = LevelSpan{ .mFirst = first, .mEnd = shape.mLevels } };
        }

        /// Every completion the hands have, in the order taken.
        std::vector<FeedArrival> takeAll(TextureHands& hands)
        {
            std::vector<FeedArrival> taken;
            while (std::optional<FeedArrival> arrival = hands.take())
                taken.push_back(std::move(*arrival));
            return taken;
        }

        /// **A loose file's chain is made in BC7 and handed over from the first level asked, in the order asked**: an 8
        /// by 8 loose colour file is planned as BC7 of four levels, 64 + 16 + 16 + 16 bytes; asked from level 0 its
        /// arrival holds all 112, and from level 2 the 32 of levels 2 and 3, level 2 at offset nought. Asked after it,
        /// a file in a format no reader here decodes fails, saying why, and arrives after the others.
        TEST(RtxTextureHandsTest, aChainIsHandedOverFromTheFirstLevelAskedInTheOrderAsked)
        {
            BufferPool pool(TextureHands::sPoolBytes);
            TextureHands hands(nullptr, pool, 2);
            const osg::ref_ptr<const osg::Image> loose = Testing::makeImage("textures/loose.dds", 8, 8);
            const osg::ref_ptr<const osg::Image> odd
                = Testing::makeImage("textures/odd.dds", 2, 2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            ASSERT_TRUE(hands.hasCredit());
            hands.ask(requestOf(loose, 1, 0));
            hands.ask(requestOf(loose, 2, 2));
            hands.ask(requestOf(odd, 3, 0));
            hands.flush();

            const std::vector<FeedArrival> taken = takeAll(hands);
            ASSERT_EQ(taken.size(), 3u);
            EXPECT_EQ(taken[0].mRequest.mTicket, FeedTicket{ 1 });
            EXPECT_EQ(taken[1].mRequest.mTicket, FeedTicket{ 2 });
            EXPECT_EQ(taken[2].mRequest.mTicket, FeedTicket{ 3 });

            const auto* whole = std::get_if<FeedMade>(&taken[0].mOutcome);
            ASSERT_NE(whole, nullptr);
            EXPECT_EQ(whole->mShape, taken[0].mRequest.mShape);
            EXPECT_EQ(whole->mTexels.mBytes.size(), 112u);
            ASSERT_EQ(whole->mLevels.size(), 4u);
            EXPECT_EQ(whole->mLevels[1].mOffset, 64u);

            const auto* coarse = std::get_if<FeedMade>(&taken[1].mOutcome);
            ASSERT_NE(coarse, nullptr);
            EXPECT_EQ(coarse->mTexels.mBytes.size(), 32u) << "levels 2 and 3 alone";
            ASSERT_EQ(coarse->mLevels.size(), 4u);
            EXPECT_EQ(coarse->mLevels[2].mOffset, 0u);
            EXPECT_EQ(coarse->mLevels[3].mOffset, 16u);

            const auto* refused = std::get_if<FeedFailed>(&taken[2].mOutcome);
            ASSERT_NE(refused, nullptr);
            EXPECT_FALSE(refused->mWhy.empty());
            EXPECT_TRUE(hands.isIdle());
        }

        /// **A request forgotten never arrives, and the hands go with requests in flight**: of three asked, the second
        /// forgotten at once arrives never, made or not, and the other two in their order; hands asked a dozen and let
        /// go before any is taken stop and drop them.
        TEST(RtxTextureHandsTest, aForgottenRequestNeverArrivesAndTheHandsGoWithRequestsInFlight)
        {
            BufferPool pool(TextureHands::sPoolBytes);
            const osg::ref_ptr<const osg::Image> loose = Testing::makeImage("textures/loose.dds", 64, 64);
            {
                TextureHands hands(nullptr, pool, 1);
                hands.ask(requestOf(loose, 1, 0));
                hands.ask(requestOf(loose, 2, 0));
                hands.forget(FeedTicket{ 2 });
                hands.ask(requestOf(loose, 3, 0));
                hands.flush();
                const std::vector<FeedArrival> taken = takeAll(hands);
                ASSERT_EQ(taken.size(), 2u);
                EXPECT_EQ(taken[0].mRequest.mTicket, FeedTicket{ 1 });
                EXPECT_EQ(taken[1].mRequest.mTicket, FeedTicket{ 3 });
            }
            {
                TextureHands going(nullptr, pool, 2);
                for (std::uint32_t at = 0; at < 12; ++at)
                    going.ask(requestOf(loose, 10 + at, 0));
            }
            EXPECT_EQ(pool.getHeld().mInUse, 0u) << "a buffer a dropped request held";
        }
    }
}
