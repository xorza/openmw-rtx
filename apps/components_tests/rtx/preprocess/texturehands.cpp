#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
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
            TextureHands hands(nullptr, pool, 2, nullptr);
            const osg::ref_ptr<const osg::Image> loose = Testing::makeImage("textures/loose.dds", 8, 8);
            const osg::ref_ptr<const osg::Image> odd
                = Testing::makeImage("textures/odd.dds", 2, 2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            ASSERT_EQ(hands.ask(requestOf(loose, 1, 0), FeedRoom::Open), FeedAdmission::Admitted);
            ASSERT_EQ(hands.ask(requestOf(loose, 2, 2), FeedRoom::Open), FeedAdmission::Admitted);
            ASSERT_EQ(hands.ask(requestOf(odd, 3, 0), FeedRoom::Open), FeedAdmission::Admitted);
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

        /// **A request is admitted with the room its texels take, and the pool never stands between the request at
        /// the front and its room** (#77). A 256-square loose file is made in BC7 to a texel, 65,536 + 16,384 + 4,096 +
        /// 1,024 + 256 + 64 + 16 + 16 + 16 = 87,408 bytes in a class of 128 KiB, and an 8-square one 112 bytes in one
        /// of 64 KiB (`pooledBytesOf`), from its third level 32; a file whose chain is not made, here one in a format
        /// no reader decodes, takes none. In a pool of 128 KiB the large file asked first takes it all, so the small
        /// one is refused, charged nothing, while the one that takes none is admitted beside it, the room closed or
        /// not. As the game takes its texels, a frame at a time, asking what fits and taking what came, every request
        /// arrives in the order asked. Before, the small ones were admitted, made first and filled the pool, and the
        /// first waited for ever on room only the ones behind it held. A closed room refuses what takes room though the
        /// pool has it, and a full ring refuses everything.
        TEST(RtxTextureHandsTest, aRequestIsAdmittedWithTheRoomItsTexelsTake)
        {
            BufferPool pool(std::size_t{ 128 } << 10);
            TextureHands hands(nullptr, pool, 2, nullptr);
            const osg::ref_ptr<const osg::Image> large = Testing::makeImage("textures/large.dds", 256, 256);
            const osg::ref_ptr<const osg::Image> small = Testing::makeImage("textures/small.dds", 8, 8);
            const osg::ref_ptr<const osg::Image> odd
                = Testing::makeImage("textures/odd.dds", 2, 2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2);
            EXPECT_EQ(pooledBytesOf(requestOf(large, 1, 0)), 87'408u);
            EXPECT_EQ(pooledBytesOf(requestOf(small, 2, 0)), 112u);
            EXPECT_EQ(pooledBytesOf(requestOf(small, 2, 2)), 32u) << "from the first level asked";
            EXPECT_EQ(pooledBytesOf(requestOf(odd, 3, 0)), 0u);

            ASSERT_EQ(hands.ask(requestOf(large, 1, 0), FeedRoom::Open), FeedAdmission::Admitted);
            EXPECT_EQ(pool.getHeld().mCharged, std::size_t{ 128 } << 10);
            EXPECT_EQ(hands.ask(requestOf(small, 2, 0), FeedRoom::Open), FeedAdmission::NoRoom)
                << "past the room the large one holds";
            EXPECT_EQ(pool.getHeld().mCharged, std::size_t{ 128 } << 10) << "a refused request charged";
            EXPECT_EQ(hands.ask(requestOf(odd, 3, 0), FeedRoom::Closed), FeedAdmission::Admitted)
                << "a request that takes no room goes past a closed room";

            std::vector<std::uint32_t> asked{ 4, 5 };
            std::vector<FeedTicket> taken;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (taken.size() < 4 && std::chrono::steady_clock::now() < deadline)
            {
                while (!asked.empty()
                    && hands.ask(requestOf(small, asked.front(), 0), FeedRoom::Open) == FeedAdmission::Admitted)
                    asked.erase(asked.begin());
                if (std::optional<FeedArrival> arrival = hands.take())
                    taken.push_back(arrival->mRequest.mTicket);
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            EXPECT_EQ(
                taken, (std::vector<FeedTicket>{ FeedTicket{ 1 }, FeedTicket{ 3 }, FeedTicket{ 4 }, FeedTicket{ 5 } }));
            EXPECT_EQ(pool.getHeld().mCharged, 0u);

            EXPECT_EQ(hands.ask(requestOf(small, 6, 0), FeedRoom::Closed), FeedAdmission::NoRoom);
            EXPECT_EQ(pool.getHeld().mCharged, 0u);
            for (std::uint32_t at = 0; at < TextureHands::sInFlightMost; ++at)
                ASSERT_EQ(hands.ask(requestOf(odd, 10 + at, 0), FeedRoom::Open), FeedAdmission::Admitted);
            EXPECT_EQ(hands.ask(requestOf(odd, 100, 0), FeedRoom::Open), FeedAdmission::NoSlot);
            EXPECT_EQ(hands.ask(requestOf(small, 101, 0), FeedRoom::Open), FeedAdmission::NoSlot)
                << "a full ring answers before the room";
            hands.flush();
        }

        /// **A request forgotten never arrives, and the hands go with requests in flight**: of three asked, the second
        /// forgotten at once arrives never, made or not, and the other two in their order; hands asked a dozen and let
        /// go before any is taken stop and drop them.
        TEST(RtxTextureHandsTest, aForgottenRequestNeverArrivesAndTheHandsGoWithRequestsInFlight)
        {
            BufferPool pool(TextureHands::sPoolBytes);
            const osg::ref_ptr<const osg::Image> loose = Testing::makeImage("textures/loose.dds", 64, 64);
            {
                TextureHands hands(nullptr, pool, 1, nullptr);
                ASSERT_EQ(hands.ask(requestOf(loose, 1, 0), FeedRoom::Open), FeedAdmission::Admitted);
                ASSERT_EQ(hands.ask(requestOf(loose, 2, 0), FeedRoom::Open), FeedAdmission::Admitted);
                hands.forget(FeedTicket{ 2 });
                ASSERT_EQ(hands.ask(requestOf(loose, 3, 0), FeedRoom::Open), FeedAdmission::Admitted);
                hands.flush();
                const std::vector<FeedArrival> taken = takeAll(hands);
                ASSERT_EQ(taken.size(), 2u);
                EXPECT_EQ(taken[0].mRequest.mTicket, FeedTicket{ 1 });
                EXPECT_EQ(taken[1].mRequest.mTicket, FeedTicket{ 3 });
            }
            {
                TextureHands going(nullptr, pool, 2, nullptr);
                for (std::uint32_t at = 0; at < 12; ++at)
                    ASSERT_EQ(going.ask(requestOf(loose, 10 + at, 0), FeedRoom::Open), FeedAdmission::Admitted);
            }
            EXPECT_EQ(pool.getHeld().mCharged, 0u) << "a buffer or a credit a dropped request held";
        }
    }
}
