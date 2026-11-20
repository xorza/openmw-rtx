#include <chrono>
#include <cmath>
#include <optional>

#include <gtest/gtest.h>

#include <components/resource/cachebudget.hpp>
#include <components/resource/systemshare.hpp>

namespace Resource
{
    namespace
    {
        constexpr std::uint64_t sGiB = std::uint64_t{ 1 } << 30;
        using Seconds = std::chrono::duration<double>;

        /// **A device whose memory is the system's may hold what it holds and what the system leaves beside it, less
        /// the host's half gigabyte**: holding 2 GiB with 5 left beside it, 6.5; holding a quarter with a half left, a
        /// quarter; holding an eighth with a quarter left, nothing. The host's half is below the caches' gigabyte, so
        /// they stop first.
        TEST(ResourceSystemShareTest, theBoundIsWhatTheDeviceHoldsAndWhatTheSystemLeavesLessTheHostsHalf)
        {
            static_assert(SystemShare::boundOf(2 * sGiB, 5 * sGiB) == 6 * sGiB + sGiB / 2);
            static_assert(SystemShare::boundOf(sGiB / 4, sGiB / 2) == sGiB / 4);
            static_assert(SystemShare::boundOf(sGiB / 8, sGiB / 4) == 0);
            static_assert(SystemShare::sHostReserveBytes < Resource::CacheBudget::sReserveBytes);
        }

        /// **The bound follows its readings over a minute, both ways**: taken whole at the first; a reading 1 GiB
        /// lower held a minute moves it `1 - 1/e` of the way, 0.632 GiB; a second's reading barely moves it, by
        /// `1 - e^(-1/60)` of the step; and back up, it follows the same way.
        TEST(ResourceSystemShareTest, theBoundFollowsItsReadingsOverAMinuteBothWays)
        {
            SystemShare share;
            EXPECT_EQ(share.getBound(), std::nullopt) << "before any reading";

            share.read(2 * sGiB, 5 * sGiB, Seconds(0.0));
            ASSERT_EQ(share.getBound(), 6 * sGiB + sGiB / 2) << "the first reading, whole";

            // The host takes a gigabyte, for a minute: the bound reads 5.5, and the average moves 1 - 1/e of the way.
            share.read(2 * sGiB, 4 * sGiB, Seconds(60.0));
            const double afterMinute = 6.5 - (1.0 - std::exp(-1.0));
            EXPECT_NEAR(static_cast<double>(*share.getBound()) / static_cast<double>(sGiB), afterMinute, 1e-9);

            // A spike of a second, three gigabytes more taken, moves it by a sixtieth's worth.
            share.read(2 * sGiB, sGiB, Seconds(1.0));
            const double afterSpike = afterMinute + (2.5 - afterMinute) * (1.0 - std::exp(-1.0 / 60.0));
            EXPECT_NEAR(static_cast<double>(*share.getBound()) / static_cast<double>(sGiB), afterSpike, 1e-9);
            EXPECT_GT(*share.getBound(), 5 * sGiB) << "a second's spike cost the device under a gigabyte";

            // Back up, the same way.
            share.read(2 * sGiB, 5 * sGiB, Seconds(60.0));
            const double backUp = afterSpike + (6.5 - afterSpike) * (1.0 - std::exp(-1.0));
            EXPECT_NEAR(static_cast<double>(*share.getBound()) / static_cast<double>(sGiB), backUp, 1e-9);
        }

        /// **What the device gave back stays claimed while the average of what it held stands above it**: holding 4
        /// GiB, then 3 a second later, it claims `e^(-1/60)` of the gigabyte it gave back; a minute on at 3, `1/e` of
        /// that; holding more than its average, nothing. **And a block given back moves no bound**: what it holds falls
        /// as what the system leaves rises, by the same bytes.
        TEST(ResourceSystemShareTest, whatTheDeviceGaveBackStaysClaimedAndMovesNoBound)
        {
            SystemShare share;
            EXPECT_EQ(share.getClaim(), 0u) << "before any reading";
            share.read(4 * sGiB, 2 * sGiB, Seconds(0.0));
            EXPECT_EQ(share.getClaim(), 0u);
            const std::optional<std::uint64_t> bound = share.getBound();

            share.read(3 * sGiB, 3 * sGiB, Seconds(1.0));
            const double claimed = std::exp(-1.0 / 60.0);
            EXPECT_NEAR(static_cast<double>(share.getClaim()) / static_cast<double>(sGiB), claimed, 1e-9);
            EXPECT_EQ(share.getBound(), bound) << "the gigabyte given back is the system's to leave the device again";

            share.read(3 * sGiB, 3 * sGiB, Seconds(60.0));
            EXPECT_NEAR(
                static_cast<double>(share.getClaim()) / static_cast<double>(sGiB), claimed * std::exp(-1.0), 1e-9);

            share.read(5 * sGiB, sGiB, Seconds(1.0));
            EXPECT_EQ(share.getClaim(), 0u) << "holding more than its average, it claims nothing";
        }
    }
}
