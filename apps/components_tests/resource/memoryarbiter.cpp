#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <gtest/gtest.h>

#include <components/resource/budgetedobjectcache.hpp>
#include <components/resource/cachebudget.hpp>
#include <components/resource/memoryarbiter.hpp>
#include <components/resource/systemshare.hpp>

namespace Resource
{
    namespace
    {
        constexpr std::uint64_t sGiB = std::uint64_t{ 1 } << 30;

        SystemReading readingAt(const std::optional<std::uint64_t> available, const std::chrono::seconds at)
        {
            return SystemReading{
                .mAvailable = available, .mHeapFree = sGiB / 2, .mAt = std::chrono::steady_clock::time_point(at)
            };
        }

        /// **A tick derives the caches' budget and the device's bound from one reading**: 6 GiB left with half a
        /// gigabyte free on the heap, nothing held, is 5.5 GiB less the reserve's one; a device that posted 2 GiB is
        /// held to its own and the 6 left, less the host's half, 7.5. With no device the bound is nothing, and a system
        /// that states nothing leaves the caches unbounded.
        TEST(ResourceMemoryArbiterTest, aTickDerivesTheCachesBudgetAndTheDevicesBoundFromOneReading)
        {
            CacheBudget budget(0);
            MemoryArbiter arbiter(budget);
            arbiter.tick([] { return readingAt(6 * sGiB, std::chrono::seconds(0)); });
            EXPECT_EQ(budget.getBytes(), 6 * sGiB + sGiB / 2 - CacheBudget::sReserveBytes);
            EXPECT_EQ(arbiter.getDeviceBound(), std::nullopt) << "no device posted";
            EXPECT_EQ(arbiter.getAvailable(), std::optional<std::uint64_t>(6 * sGiB));

            arbiter.postDeviceHeld(2 * sGiB);
            arbiter.take(readingAt(6 * sGiB, std::chrono::seconds(1)));
            EXPECT_EQ(arbiter.getDeviceBound(), std::optional<std::uint64_t>(SystemShare::boundOf(2 * sGiB, 6 * sGiB)));
            EXPECT_EQ(*arbiter.getDeviceBound(), 8 * sGiB - SystemShare::sHostReserveBytes);

            arbiter.take(readingAt(std::nullopt, std::chrono::seconds(2)));
            EXPECT_EQ(budget.getBytes(), CacheBudget::sUnbounded);
            // How long each read took is kept, the slowest since the last ask: a read held for 3 ms is at least that.
            std::ignore = arbiter.takeSlowestRead();
            arbiter.tick([] {
                const std::chrono::steady_clock::time_point until
                    = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
                while (std::chrono::steady_clock::now() < until)
                {
                }
                return readingAt(6 * sGiB, std::chrono::seconds(3));
            });
            EXPECT_GE(arbiter.takeSlowestRead(), std::chrono::milliseconds(3));
            EXPECT_EQ(arbiter.takeSlowestRead(), std::chrono::nanoseconds{}) << "taken";
        }

        /// **A tick destroys what the caches evicted before it reads the system**: alive, an evicted value is in no
        /// term of the budget. A value of 40 evicted by an insertion past a budget of 60 is gone by the time the
        /// reading is taken.
        TEST(ResourceMemoryArbiterTest, aTickDestroysTheEvictedBeforeItReads)
        {
            CacheBudget budget(60);
            BudgetedObjectCache<std::int32_t, std::shared_ptr<const std::string>> cache(budget);
            const auto loadOf = [](std::uint64_t bytes) {
                return [bytes] {
                    return Loaded<std::shared_ptr<const std::string>>{ std::make_shared<const std::string>("loaded"),
                        bytes };
                };
            };
            const std::weak_ptr<const std::string> first = cache.getOrLoad(1, loadOf(40));
            cache.getOrLoad(2, loadOf(40));
            ASSERT_FALSE(first.expired()) << "evicted, and alive until a tick";

            MemoryArbiter arbiter(budget);
            bool destroyedFirst = false;
            arbiter.tick([&] {
                destroyedFirst = first.expired();
                return readingAt(std::nullopt, std::chrono::seconds(0));
            });
            EXPECT_TRUE(destroyedFirst);
        }

        /// **A claim the device makes ends as ticks go by, whatever arrives**: a device that held 4 GiB and gives 2
        /// back claims what its average stands above what it holds, which the caches leave it; half an hour of ticks
        /// with nothing else posted brings the average down to its 2, the claim `2 GiB × e^(-1800 / 60)` under a byte,
        /// where a claim read only as content arrived stood withheld from the caches for good.
        TEST(ResourceMemoryArbiterTest, aClaimTheDeviceMakesEndsAsTicksGoBy)
        {
            CacheBudget budget(0);
            MemoryArbiter arbiter(budget);
            arbiter.postDeviceHeld(4 * sGiB);
            arbiter.take(readingAt(8 * sGiB, std::chrono::seconds(0)));
            const std::uint64_t free = 8 * sGiB + sGiB / 2 - CacheBudget::sReserveBytes;
            EXPECT_EQ(budget.getBytes(), free) << "no claim on the first reading";

            arbiter.postDeviceHeld(2 * sGiB);
            arbiter.take(readingAt(10 * sGiB, std::chrono::seconds(1)));
            const std::uint64_t claimed = 10 * sGiB + sGiB / 2 - CacheBudget::sReserveBytes - budget.getBytes();
            EXPECT_GT(claimed, 0u) << "a block given back is claimed";

            for (std::int64_t second = 2; second <= 1800; ++second)
                arbiter.take(readingAt(10 * sGiB, std::chrono::seconds(second)));
            EXPECT_EQ(budget.getBytes(), 10 * sGiB + sGiB / 2 - CacheBudget::sReserveBytes) << "the claim ended";
        }
    }
}
