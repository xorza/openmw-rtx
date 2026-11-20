#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/bound.hpp>
#include <components/resource/budgetedobjectcache.hpp>
#include <components/resource/cachebudget.hpp>

namespace Resource
{
    namespace
    {
        // The point of the value type parameter: a type that derives from nothing at all is cacheable, and the budget
        // still knows whether anything outside holds it.
        using SharedCache = BudgetedObjectCache<std::int32_t, std::shared_ptr<const std::string>>;
        using LoadedString = Loaded<std::shared_ptr<const std::string>>;

        /// Spins until `ready` answers, at most two seconds, which a test past would have hung on.
        template <class Ready>
        bool awaitWithin(Ready ready)
        {
            for (const Rtx::Testing::WaitBound bound(std::chrono::seconds(2)); !ready();)
            {
                if (bound.passed())
                    return false;
                std::this_thread::yield();
            }
            return true;
        }

        /// **Eight threads asking one key load it once**: the first loads, and the seven that ask while it loads wait
        /// for that load and take its value. The load holds until all seven wait, so each is counted.
        TEST(ResourceBudgetedObjectCacheTest, getOrLoadLoadsAKeyOnceForEveryThreadAskingAtOnce)
        {
            CacheBudget budget(CacheBudget::sUnbounded);
            SharedCache cache(budget);
            std::atomic<std::int32_t> loads = 0;
            const auto load = [&] {
                ++loads;
                EXPECT_TRUE(awaitWithin([&] { return cache.getStats().mCoalesced == 7; }));
                return LoadedString{ std::make_shared<const std::string>("loaded") };
            };

            std::vector<std::shared_ptr<const std::string>> got(8);
            std::vector<std::jthread> threads;
            threads.reserve(got.size());
            for (std::shared_ptr<const std::string>& slot : got)
                threads.emplace_back([&, &slot = slot] { slot = cache.getOrLoad(42, load); });
            threads.clear();

            EXPECT_EQ(loads, 1);
            for (const auto& value : got)
                EXPECT_EQ(value, got.front());
            EXPECT_EQ(*got.front(), "loaded");
            EXPECT_EQ(cache.getRefFromObjectCache(42), got.front());
            const CacheStats stats = cache.getStats();
            EXPECT_EQ(stats.mLoaded, 1);
            EXPECT_EQ(stats.mCoalesced, 7);
            EXPECT_EQ(stats.mReloaded, 0);

            // A key in the cache is a hit, and no load.
            EXPECT_EQ(cache.getOrLoad(42, load), got.front());
            EXPECT_EQ(loads, 1);
        }

        /// **A load that throws answers every waiter with its exception and leaves nothing behind**: no entry, no
        /// load in flight, so the next ask loads again.
        ///
        /// **The loader holds its catch until the waiter is joined.** Both catch one exception object, which
        /// `std::rethrow_exception` shares; the last to end its catch frees it, through a count in libstdc++, which is
        /// built without ThreadSanitizer. Where the loader ended last, the free was ordered after the waiter's read by
        /// that count alone, which the sanitizer cannot see, and it reported a race.
        TEST(ResourceBudgetedObjectCacheTest, getOrLoadAnswersAThrowingLoadToItsWaitersAndLoadsAgainAfter)
        {
            CacheBudget budget(CacheBudget::sUnbounded);
            SharedCache cache(budget);
            std::atomic<std::int32_t> loads = 0;
            const auto failing = [&]() -> LoadedString {
                ++loads;
                EXPECT_TRUE(awaitWithin([&] { return cache.getStats().mCoalesced == 1; }));
                throw std::runtime_error("unreadable");
            };

            std::optional<std::string> waiterSaw;
            bool loaderThrew = false;
            std::latch waiterJoined(1);
            std::jthread loader([&] {
                try
                {
                    cache.getOrLoad(42, failing);
                }
                catch (const std::runtime_error&)
                {
                    loaderThrew = true;
                    waiterJoined.wait();
                }
                catch (...)
                {
                    ADD_FAILURE() << "the load threw something other than its own exception";
                }
            });
            EXPECT_TRUE(awaitWithin([&] { return loads == 1; }));
            std::jthread waiter([&] {
                try
                {
                    cache.getOrLoad(42, failing);
                }
                catch (const std::runtime_error& error)
                {
                    waiterSaw = error.what();
                }
            });
            waiter.join();
            waiterJoined.count_down();
            loader.join();

            EXPECT_TRUE(loaderThrew);
            EXPECT_EQ(waiterSaw, "unreadable");
            EXPECT_EQ(loads, 1);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(42), std::nullopt);
            EXPECT_EQ(
                *cache.getOrLoad(42, [] { return LoadedString{ std::make_shared<const std::string>("second") }; }),
                "second");
            EXPECT_EQ(cache.getStats().mLoaded, 1);
        }

        /// **Two keys load at once**: the load of one does not wait on the load of the other.
        TEST(ResourceBudgetedObjectCacheTest, getOrLoadLoadsTwoKeysInParallel)
        {
            CacheBudget budget(CacheBudget::sUnbounded);
            SharedCache cache(budget);
            std::atomic<std::int32_t> started = 0;
            const auto load = [&] {
                ++started;
                EXPECT_TRUE(awaitWithin([&] { return started == 2; })) << "one key's load waited on the other's";
                return LoadedString{ std::make_shared<const std::string>("loaded") };
            };
            std::jthread first([&] { cache.getOrLoad(1, load); });
            std::jthread second([&] { cache.getOrLoad(2, load); });
            first.join();
            second.join();
            EXPECT_EQ(cache.getStats().mLoaded, 2);
            EXPECT_EQ(cache.getStats().mCoalesced, 0);
        }

        /// **A key loaded again after the cache let it go is a reload**, whichever way it went: removed, evicted or
        /// cleared; a first load is none.
        TEST(ResourceBudgetedObjectCacheTest, aKeyLoadedAgainAfterItWentIsAReload)
        {
            CacheBudget budget(CacheBudget::sUnbounded);
            SharedCache cache(budget);
            const auto load = [] { return LoadedString{ std::make_shared<const std::string>("loaded"), 1 }; };
            cache.getOrLoad(1, load);
            cache.getOrLoad(2, load);
            cache.getOrLoad(3, load);
            EXPECT_EQ(cache.getStats().mReloaded, 0);

            // 1 removed, and 2 and 3 evicted under a budget held to nothing, then let follow the system again.
            cache.removeFromObjectCache(1);
            budget.limit(0);
            budget.limit(std::nullopt);
            EXPECT_EQ(cache.getStats().mSize, 0);
            cache.getOrLoad(1, load);
            cache.getOrLoad(2, load);
            EXPECT_EQ(cache.getStats().mReloaded, 2);
            EXPECT_EQ(cache.getStats().mLoaded, 5);

            cache.clear();
            cache.getOrLoad(1, load);
            EXPECT_EQ(cache.getStats().mReloaded, 3);
            cache.getOrLoad(4, load);
            EXPECT_EQ(cache.getStats().mReloaded, 3) << "a key never held is no reload";
        }

        /// A load of `bytes` that answers "loaded".
        auto loadOf(std::uint64_t bytes)
        {
            return [bytes] { return LoadedString{ std::make_shared<const std::string>("loaded"), bytes }; };
        }

        /// **Past its budget, a cache lets go of the least recently used entry nothing holds first**, as many as
        /// make room and no more: 40 + 40 + 40 bytes in a budget of 100 lets the first go, and 80 stay.
        TEST(ResourceBudgetedObjectCacheTest, pastItsBudgetTheLeastRecentlyUsedUnheldEntryGoesFirst)
        {
            CacheBudget budget(100);
            SharedCache cache(budget);
            cache.getOrLoad(1, loadOf(40));
            cache.getOrLoad(2, loadOf(40));
            EXPECT_EQ(budget.getHeld(), 80);
            cache.getOrLoad(3, loadOf(40));

            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(1), std::nullopt);
            EXPECT_NE(cache.getRefFromObjectCacheOrNone(2), std::nullopt);
            EXPECT_NE(cache.getRefFromObjectCacheOrNone(3), std::nullopt);
            EXPECT_EQ(budget.getHeld(), 80);
            const CacheStats stats = cache.getStats();
            EXPECT_EQ(stats.mBytes, 80);
            EXPECT_EQ(stats.mExpired, 1);
            EXPECT_EQ(stats.mSize, 2);

            // **What went is destroyed by the worker that derives the budget**, not the thread that inserted: the
            // first value lives until `makeRoom`, watched here through a weak hold.
            CacheBudget watched(100);
            SharedCache deferred(watched);
            const std::weak_ptr<const std::string> first = deferred.getOrLoad(1, loadOf(40));
            const std::weak_ptr<const std::string> second = deferred.getOrLoad(2, loadOf(40));
            deferred.getOrLoad(3, loadOf(40));
            EXPECT_EQ(deferred.getRefFromObjectCacheOrNone(1), std::nullopt) << "evicted";
            EXPECT_FALSE(first.expired()) << "destroyed by the inserting thread";
            watched.makeRoom();
            EXPECT_TRUE(first.expired()) << "kept past the deriving worker's look";

            // **And before the worker reads the system's memory for the budget**, by `destroyEvicted` alone, which
            // evicts nothing: the second goes for a fourth, is destroyed, and the two left stay.
            deferred.getOrLoad(4, loadOf(40));
            EXPECT_EQ(deferred.getRefFromObjectCacheOrNone(2), std::nullopt) << "evicted";
            EXPECT_FALSE(second.expired());
            watched.destroyEvicted();
            EXPECT_TRUE(second.expired());
            EXPECT_EQ(deferred.getStats().mSize, 2);
            EXPECT_EQ(watched.getHeld(), 80);

            // **A removal leaves by the same door**: the third, removed, lives until the worker destroys it.
            const std::weak_ptr<const std::string> third = *deferred.getRefFromObjectCacheOrNone(3);
            deferred.removeFromObjectCache(3);
            EXPECT_EQ(watched.getHeld(), 40);
            EXPECT_FALSE(third.expired()) << "destroyed under the cache's lock by the remover";
            watched.destroyEvicted();
            EXPECT_TRUE(third.expired());

            // The same three under a budget of 120 keep all three: the budget is what decides.
            CacheBudget roomy(120);
            SharedCache kept(roomy);
            for (const std::int32_t key : { 1, 2, 3 })
                kept.getOrLoad(key, loadOf(40));
            EXPECT_EQ(kept.getStats().mSize, 3);
            EXPECT_EQ(kept.getStats().mExpired, 0);
        }

        /// **An update stamps every held entry, in runs with the lock let go between them**: 600 entries of a byte, the
        /// oldest 300 held while the cache updates and let go after, past the first run of 256: held then, they are the
        /// newest, so a budget of 300 lets the other 300 go and keeps every one of them.
        TEST(ResourceBudgetedObjectCacheTest, anUpdateStampsEveryHeldEntryAcrossItsRuns)
        {
            constexpr std::int32_t sEntries = 600;
            constexpr std::int32_t sHeld = 300;
            CacheBudget budget(sEntries);
            SharedCache cache(budget);
            std::vector<std::shared_ptr<const std::string>> held;
            for (std::int32_t key = 0; key < sEntries; ++key)
            {
                std::shared_ptr<const std::string> value = cache.getOrLoad(key, loadOf(1));
                if (key < sHeld)
                    held.push_back(std::move(value));
            }
            cache.update(1.0, 0.0);
            held.clear();

            budget.limit(sHeld);
            for (std::int32_t key = 0; key < sEntries; ++key)
                EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key).has_value(), key < sHeld) << key;
        }

        /// **An insertion evicts as many bytes as it added, and the look the rest**: four entries of 30 under a budget
        /// of 200, then held to 60 from outside; a load of 10 evicts the oldest, 30, and stops past its 10, 100 held;
        /// the budget's own look takes it to 60.
        TEST(ResourceBudgetedObjectCacheTest, anInsertionEvictsAsManyBytesAsItAdded)
        {
            CacheBudget budget(200);
            SharedCache cache(budget);
            for (std::int32_t key = 1; key <= 4; ++key)
                cache.getOrLoad(key, loadOf(30));
            budget.yieldTo(140);
            EXPECT_EQ(budget.getHeld(), 120) << "the claim makes no room on its own";

            cache.getOrLoad(5, loadOf(10));
            EXPECT_EQ(budget.getHeld(), 100) << "the oldest of 30 for an insertion of 10";
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(1), std::nullopt);
            EXPECT_NE(cache.getRefFromObjectCacheOrNone(2), std::nullopt);

            budget.makeRoom();
            EXPECT_EQ(budget.getHeld(), 40) << "30 and 30 more, the look stopping at the budget of 60";
        }

        /// **A hit moves an entry to the front**: 1 asked again after 2 was loaded makes 2 the one to go.
        TEST(ResourceBudgetedObjectCacheTest, aHitMovesAnEntryToTheFront)
        {
            CacheBudget budget(100);
            SharedCache cache(budget);
            cache.getOrLoad(1, loadOf(40));
            cache.getOrLoad(2, loadOf(40));
            cache.getRefFromObjectCache(1);
            cache.getOrLoad(3, loadOf(40));

            EXPECT_NE(cache.getRefFromObjectCacheOrNone(1), std::nullopt);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(2), std::nullopt);
        }

        /// **An entry anything holds never goes**, however old: the oldest unheld goes in its place, and where
        /// everything is held the cache stands past its budget until something is let go.
        TEST(ResourceBudgetedObjectCacheTest, anEntryAnythingHoldsNeverGoes)
        {
            CacheBudget budget(100);
            SharedCache cache(budget);
            const std::shared_ptr<const std::string> first = cache.getOrLoad(1, loadOf(40));
            cache.getOrLoad(2, loadOf(40));
            cache.getOrLoad(3, loadOf(40));
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(1), first);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(2), std::nullopt);

            std::shared_ptr<const std::string> third = cache.getRefFromObjectCache(3);
            // Held by the test, as 1 and 3 are.
            const std::shared_ptr<const std::string> fourth = cache.getOrLoad(4, loadOf(40));
            EXPECT_EQ(budget.getHeld(), 120) << "all three held, past the budget";
            EXPECT_EQ(cache.getStats().mSize, 3);
            EXPECT_FALSE(budget.hasRoom()) << "no room for a preload";
            EXPECT_EQ(budget.getStarvedLooks(), 1u) << "the load of 4 looked, and found all in use";

            // An insertion past a budget all in use looks no more until the caches have updated and the budget
            // looks again.
            third.reset();
            cache.getOrLoad(5, loadOf(0));
            EXPECT_EQ(cache.getStats().mSize, 4) << "not looked for";
            EXPECT_EQ(budget.getStarvedLooks(), 1u);
            cache.update(2.0, 0.0);
            EXPECT_EQ(budget.getHeld(), 120) << "an update stamps, and looks for no room";
            budget.makeRoom();
            EXPECT_EQ(budget.getHeld(), 80);
            EXPECT_TRUE(budget.hasRoom()) << "room again once something was let go";
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(3), std::nullopt) << "the oldest unheld after the update";
            EXPECT_NE(cache.getRefFromObjectCacheOrNone(1), std::nullopt);
        }

        /// **What anything holds at an update is used then**: 1, held at the update and let go after, is newer
        /// than 2, which nothing held, so 2 goes first.
        TEST(ResourceBudgetedObjectCacheTest, anUpdateStampsWhatIsHeldAsUsed)
        {
            CacheBudget budget(100);
            SharedCache cache(budget);
            std::shared_ptr<const std::string> first = cache.getOrLoad(1, loadOf(40));
            cache.getOrLoad(2, loadOf(40));
            cache.update(1.0, 0.0);
            first.reset();
            cache.getOrLoad(3, loadOf(40));

            EXPECT_NE(cache.getRefFromObjectCacheOrNone(1), std::nullopt);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(2), std::nullopt);
            EXPECT_EQ(cache.getStats().mExpired, 1) << "an update lets nothing go by time";
        }

        /// **One order over every cache of a budget**: whatever was used longest ago goes, whichever cache holds
        /// it.
        TEST(ResourceBudgetedObjectCacheTest, oneBudgetKeepsOneOrderOverItsCaches)
        {
            CacheBudget budget(100);
            SharedCache images(budget);
            SharedCache models(budget);
            images.getOrLoad(1, loadOf(40));
            models.getOrLoad(1, loadOf(40));
            images.getOrLoad(2, loadOf(40));
            // Counted, not asked: an ask is a use, which would move what it asks for.
            EXPECT_EQ(images.getStats().mExpired, 1) << "the images' 1, used first";
            EXPECT_EQ(models.getStats().mExpired, 0);

            models.getOrLoad(2, loadOf(40));
            EXPECT_EQ(images.getStats().mExpired, 1);
            EXPECT_EQ(models.getStats().mExpired, 1) << "the models' 1, used before the images' 2";
            EXPECT_EQ(budget.getHeld(), 80);
            EXPECT_NE(images.getRefFromObjectCacheOrNone(2), std::nullopt);
            EXPECT_NE(models.getRefFromObjectCacheOrNone(2), std::nullopt);
        }

        /// **What a cache lets go of leaves the budget**, removed, cleared or gone with the cache, and a budget
        /// made smaller makes room at once. **A limit holds it under the system's figure** (`CacheBudget::limit`):
        /// limited to 50, the 70 held goes at once and a figure of 1000 stands at 50; lifted, the next figure stands.
        TEST(ResourceBudgetedObjectCacheTest, whatACacheLetsGoLeavesItsBudget)
        {
            CacheBudget budget(1000);
            {
                SharedCache cache(budget);
                cache.getOrLoad(1, loadOf(40));
                cache.getOrLoad(2, loadOf(30));
                cache.getOrLoad(3, loadOf(20));
                cache.removeFromObjectCache(1);
                EXPECT_EQ(budget.getHeld(), 50);

                budget.setBytes(25);
                EXPECT_EQ(budget.getHeld(), 20) << "2, the older, went";
                EXPECT_NE(cache.getRefFromObjectCacheOrNone(3), std::nullopt);

                cache.clear();
                EXPECT_EQ(budget.getHeld(), 0);
                EXPECT_EQ(cache.getStats().mBytes, 0);

                budget.setBytes(1000);
                cache.getOrLoad(4, loadOf(70));
                EXPECT_EQ(budget.getHeld(), 70);

                budget.limit(50);
                EXPECT_EQ(budget.getBytes(), 50);
                EXPECT_EQ(budget.getHeld(), 0) << "the limit made no room";
                budget.setBytes(1000);
                EXPECT_EQ(budget.getBytes(), 50) << "the system's figure passed the limit";
                budget.setBytes(30);
                EXPECT_EQ(budget.getBytes(), 30) << "a figure under the limit stands";
                budget.limit(std::nullopt);
                EXPECT_EQ(budget.getBytes(), 30) << "lifted, the system's last figure stands at once";
                budget.setBytes(1000);
                EXPECT_EQ(budget.getBytes(), 1000);

                // What the device claims is taken off the system's figure, at once and from every figure after it,
                // and makes no room until the next look.
                cache.getOrLoad(5, loadOf(70));
                budget.yieldTo(960);
                EXPECT_EQ(budget.getBytes(), 40) << "1000 less the device's 960";
                EXPECT_EQ(budget.getHeld(), 70) << "the claim made room on the frame";
                budget.setBytes(1100);
                EXPECT_EQ(budget.getBytes(), 140) << "a new figure less the same claim";
                EXPECT_EQ(budget.getHeld(), 70) << "under 140, nothing went";
                budget.yieldTo(2000);
                EXPECT_EQ(budget.getBytes(), 0) << "a claim past the figure leaves nothing";
                budget.limit(30);
                EXPECT_EQ(budget.getBytes(), 0) << "the limit takes nothing back from the claim";
                budget.limit(std::nullopt);
                budget.yieldTo(0);
                EXPECT_EQ(budget.getBytes(), 1100);
            }
            EXPECT_EQ(budget.getHeld(), 0) << "the cache's own went with it";
        }

        /// **A cache and its budget count an entry in one step**: cleared on one thread while another loads, the
        /// budget is never told an entry went before it was told the entry came. Each of the 20,000 loads is a byte,
        /// so the budget holds 20,000 at the most, where one counted gone first wraps it round past 2^63.
        TEST(ResourceBudgetedObjectCacheTest, aClearBesideALoadNeverTakesFromTheBudgetWhatItWasNotGiven)
        {
            constexpr std::int32_t sLoads = 20000;
            CacheBudget budget(CacheBudget::sUnbounded);
            SharedCache cache(budget);
            std::atomic<bool> loaded = false;
            std::jthread loader([&] {
                for (std::int32_t key = 0; key < sLoads; ++key)
                    cache.getOrLoad(key, loadOf(1));
                loaded = true;
            });

            std::uint64_t heldMost = 0;
            while (!loaded)
            {
                cache.clear();
                heldMost = std::max(heldMost, budget.getHeld());
            }
            loader.join();

            EXPECT_LE(heldMost, static_cast<std::uint64_t>(sLoads));
            cache.clear();
            EXPECT_EQ(budget.getHeld(), 0);
        }
    }
}
