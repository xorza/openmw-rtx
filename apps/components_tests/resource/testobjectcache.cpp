#include <components/resource/objectcache.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <osg/Object>

namespace Resource
{
    namespace
    {
        using namespace ::testing;

        using ObjectCache = GenericObjectCache<int, osg::ref_ptr<osg::Object>>;
        using StringObjectCache = GenericObjectCache<std::string, osg::ref_ptr<osg::Object>>;

        // The point of the value type parameter: a type that derives from nothing at all
        // is cacheable, and expiry still knows whether anything outside holds it.
        using SharedCache = GenericObjectCache<int, std::shared_ptr<const std::string>>;
        using LoadedString = Loaded<std::shared_ptr<const std::string>>;

        TEST(ResourceGenericObjectCacheTest, getRefFromObjectCacheShouldReturnNullptrByDefault)
        {
            ObjectCache cache;
            EXPECT_EQ(cache.getRefFromObjectCache(42), nullptr);
        }

        TEST(ResourceGenericObjectCacheTest, getRefFromObjectCacheOrNoneShouldReturnNulloptByDefault)
        {
            ObjectCache cache;
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(42), std::nullopt);
        }

        struct Object : osg::Object
        {
            Object() = default;

            Object(const Object& other, const osg::CopyOp& copyOp = osg::CopyOp())
                : osg::Object(other, copyOp)
            {
            }
            ~Object() override = default;
            Object& operator=(const Object&) = delete;

            META_Object(ResourceTest, Object)
        };

        TEST(ResourceGenericObjectCacheTest, shouldStoreValuesThatAreNotOsgObjects)
        {
            SharedCache cache;
            const auto value = std::make_shared<const std::string>("value");
            cache.addEntryToObjectCache(42, value);
            EXPECT_EQ(cache.getRefFromObjectCache(42), value);
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldNotDereferenceNullValues)
        {
            ObjectCache cache;
            cache.addEntryToObjectCache(42, nullptr);
            cache.update(1000, 1);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(42), Optional(nullptr));
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldKeepSharedPtrValuesHeldElsewhere)
        {
            SharedCache cache;
            const auto held = std::make_shared<const std::string>("held");
            cache.addEntryToObjectCache(1, held);
            cache.addEntryToObjectCache(2, std::make_shared<const std::string>("dropped"));

            const double referenceTime = 1000;
            cache.update(referenceTime, 1);
            cache.update(referenceTime + 10, 1);

            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(1), Optional(held));
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(2), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, shouldStoreValues)
        {
            ObjectCache cache;
            const int key = 42;
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            EXPECT_EQ(cache.getRefFromObjectCache(key), value);
        }

        TEST(ResourceGenericObjectCacheTest, shouldStoreNullptrValues)
        {
            ObjectCache cache;
            const int key = 42;
            cache.addEntryToObjectCache(key, nullptr);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(nullptr));
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldExtendLifetimeForItemsWithZeroTimestamp)
        {
            ObjectCache cache;

            const int key = 42;
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value, 0);
            value = nullptr;

            const double referenceTime = 1000;
            const double expiryDelay = 1;
            cache.update(referenceTime, expiryDelay);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));
        }

        TEST(ResourceGenericObjectCacheTest, addEntryToObjectCacheShouldReplaceExistingItemByKey)
        {
            ObjectCache cache;

            const int key = 42;
            osg::ref_ptr<Object> value1(new Object);
            osg::ref_ptr<Object> value2(new Object);
            cache.addEntryToObjectCache(key, value1);
            ASSERT_EQ(cache.getRefFromObjectCache(key), value1);
            cache.addEntryToObjectCache(key, value2);
            EXPECT_EQ(cache.getRefFromObjectCache(key), value2);
        }

        TEST(ResourceGenericObjectCacheTest, addEntryToObjectCacheShouldMarkLifetime)
        {
            ObjectCache cache;

            const double referenceTime = 1;
            const double expiryDelay = 2;

            const int key = 42;
            cache.addEntryToObjectCache(key, nullptr, referenceTime + expiryDelay);

            cache.update(referenceTime, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + expiryDelay, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + 2 * expiryDelay, expiryDelay);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldRemoveExpiredItems)
        {
            ObjectCache cache;

            const double referenceTime = 1;
            const double expiryDelay = 1;

            const int key = 42;
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            value = nullptr;

            cache.update(referenceTime, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));
            ASSERT_EQ(cache.getStats().mExpired, 0);

            cache.update(referenceTime + expiryDelay, expiryDelay);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key), std::nullopt);
            ASSERT_EQ(cache.getStats().mExpired, 1);
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldKeepExternallyReferencedItems)
        {
            ObjectCache cache;

            const double referenceTime = 1;
            const double expiryDelay = 1;

            const int key = 42;
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);

            cache.update(referenceTime, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + expiryDelay, expiryDelay);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(value));
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldKeepNotExpiredItems)
        {
            ObjectCache cache;

            const double referenceTime = 1;
            const double expiryDelay = 2;

            const int key = 42;
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            value = nullptr;

            cache.update(referenceTime + expiryDelay, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + expiryDelay / 2, expiryDelay);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));
        }

        TEST(ResourceGenericObjectCacheTest, updateShouldKeepNotExpiredNullptrItems)
        {
            ObjectCache cache;

            const double referenceTime = 1;
            const double expiryDelay = 2;

            const int key = 42;
            cache.addEntryToObjectCache(key, nullptr);

            cache.update(referenceTime + expiryDelay, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + expiryDelay / 2, expiryDelay);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));
        }

        TEST(ResourceGenericObjectCacheTest, getRefFromObjectCacheOrNoneShouldNotExtendItemLifetime)
        {
            ObjectCache cache;

            const double referenceTime = 1;
            const double expiryDelay = 2;

            const int key = 42;
            cache.addEntryToObjectCache(key, nullptr);

            cache.update(referenceTime, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + expiryDelay / 2, expiryDelay);
            ASSERT_THAT(cache.getRefFromObjectCacheOrNone(key), Optional(_));

            cache.update(referenceTime + expiryDelay, expiryDelay);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, lowerBoundShouldSupportHeterogeneousLookup)
        {
            StringObjectCache cache;
            cache.addEntryToObjectCache("a", nullptr);
            cache.addEntryToObjectCache("c", nullptr);
            EXPECT_THAT(cache.lowerBound(std::string_view("b")), Optional(Pair("c", _)));
        }

        TEST(ResourceGenericObjectCacheTest, shouldSupportRemovingItems)
        {
            ObjectCache cache;
            const int key = 42;
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            ASSERT_EQ(cache.getRefFromObjectCache(key), value);
            cache.removeFromObjectCache(key);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, clearShouldRemoveAllItems)
        {
            ObjectCache cache;

            const int key1 = 42;
            const int key2 = 13;
            osg::ref_ptr<Object> value1(new Object);
            osg::ref_ptr<Object> value2(new Object);
            cache.addEntryToObjectCache(key1, value1);
            cache.addEntryToObjectCache(key2, value2);

            ASSERT_EQ(cache.getRefFromObjectCache(key1), value1);
            ASSERT_EQ(cache.getRefFromObjectCache(key2), value2);

            cache.clear();

            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key1), std::nullopt);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key2), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, callShouldIterateOverAllItems)
        {
            ObjectCache cache;

            osg::ref_ptr<Object> value1(new Object);
            osg::ref_ptr<Object> value2(new Object);
            osg::ref_ptr<Object> value3(new Object);
            cache.addEntryToObjectCache(1, value1);
            cache.addEntryToObjectCache(2, value2);
            cache.addEntryToObjectCache(3, value3);

            std::vector<std::pair<int, osg::Object*>> actual;
            cache.call([&](int key, osg::Object* value) { actual.emplace_back(key, value); });

            EXPECT_THAT(actual, ElementsAre(Pair(1, value1.get()), Pair(2, value2.get()), Pair(3, value3.get())));
        }

        TEST(ResourceGenericObjectCacheTest, getStatsShouldReturnNumberOrAddedItems)
        {
            ObjectCache cache;

            osg::ref_ptr<Object> value1(new Object);
            osg::ref_ptr<Object> value2(new Object);
            cache.addEntryToObjectCache(13, value1);
            cache.addEntryToObjectCache(42, value2);

            const CacheStats stats = cache.getStats();

            EXPECT_EQ(stats.mSize, 2);
        }

        TEST(ResourceGenericObjectCacheTest, getStatsShouldReturnNumberOrGetsAndHits)
        {
            ObjectCache cache;

            {
                const CacheStats stats = cache.getStats();

                EXPECT_EQ(stats.mGet, 0);
                EXPECT_EQ(stats.mHit, 0);
            }

            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(13, value);
            cache.getRefFromObjectCache(13);
            cache.getRefFromObjectCache(42);

            {
                const CacheStats stats = cache.getStats();

                EXPECT_EQ(stats.mGet, 2);
                EXPECT_EQ(stats.mHit, 1);
            }
        }

        TEST(ResourceGenericObjectCacheTest, lowerBoundShouldReturnFirstNotLessThatGivenKey)
        {
            ObjectCache cache;

            osg::ref_ptr<Object> value1(new Object);
            osg::ref_ptr<Object> value2(new Object);
            osg::ref_ptr<Object> value3(new Object);
            cache.addEntryToObjectCache(1, value1);
            cache.addEntryToObjectCache(2, value2);
            cache.addEntryToObjectCache(4, value3);

            EXPECT_THAT(cache.lowerBound(3), Optional(Pair(4, value3)));
        }

        TEST(ResourceGenericObjectCacheTest, lowerBoundShouldReturnNulloptWhenKeyIsGreaterThanAnyOther)
        {
            ObjectCache cache;

            osg::ref_ptr<Object> value1(new Object);
            osg::ref_ptr<Object> value2(new Object);
            osg::ref_ptr<Object> value3(new Object);
            cache.addEntryToObjectCache(1, value1);
            cache.addEntryToObjectCache(2, value2);
            cache.addEntryToObjectCache(3, value3);

            EXPECT_EQ(cache.lowerBound(4), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, addEntryToObjectCacheShouldSupportHeterogeneousLookup)
        {
            StringObjectCache cache;
            const std::string key = "key";
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(std::string_view("key"), value);
            EXPECT_EQ(cache.getRefFromObjectCache(key), value);
        }

        TEST(ResourceGenericObjectCacheTest, addEntryToObjectCacheShouldKeyMoving)
        {
            StringObjectCache cache;
            std::string key(128, 'a');
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(std::move(key), value);
            // The test reads the moved-from object, whose state is what it checks.
            // NOLINTNEXTLINE(bugprone-use-after-move)
            EXPECT_EQ(key, "");
            EXPECT_EQ(cache.getRefFromObjectCache(std::string(128, 'a')), value);
        }

        TEST(ResourceGenericObjectCacheTest, removeFromObjectCacheShouldSupportHeterogeneousLookup)
        {
            StringObjectCache cache;
            const std::string key = "key";
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            ASSERT_EQ(cache.getRefFromObjectCache(key), value);
            cache.removeFromObjectCache(std::string_view("key"));
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(key), std::nullopt);
        }

        TEST(ResourceGenericObjectCacheTest, getRefFromObjectCacheShouldSupportHeterogeneousLookup)
        {
            StringObjectCache cache;
            const std::string key = "key";
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            EXPECT_EQ(cache.getRefFromObjectCache(std::string_view("key")), value);
        }

        TEST(ResourceGenericObjectCacheTest, getRefFromObjectCacheOrNoneShouldSupportHeterogeneousLookup)
        {
            StringObjectCache cache;
            const std::string key = "key";
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            EXPECT_THAT(cache.getRefFromObjectCacheOrNone(std::string_view("key")), Optional(value));
        }

        TEST(ResourceGenericObjectCacheTest, checkInObjectCacheShouldSupportHeterogeneousLookup)
        {
            StringObjectCache cache;
            const std::string key = "key";
            osg::ref_ptr<Object> value(new Object);
            cache.addEntryToObjectCache(key, value);
            EXPECT_TRUE(cache.checkInObjectCache(std::string_view("key"), 0));
        }

        /// Spins until `ready` answers, at most two seconds, which a test past would have hung on.
        template <class Ready>
        bool awaitWithin(Ready ready)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!ready())
            {
                if (std::chrono::steady_clock::now() > until)
                    return false;
                std::this_thread::yield();
            }
            return true;
        }

        /// **Eight threads asking one key load it once**: the first loads, and the seven that ask while it loads wait
        /// for that load and take its value. The load holds until all seven wait, so each is counted.
        TEST(ResourceGenericObjectCacheTest, getOrLoadLoadsAKeyOnceForEveryThreadAskingAtOnce)
        {
            SharedCache cache;
            std::atomic<int> loads = 0;
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
        TEST(ResourceGenericObjectCacheTest, getOrLoadAnswersAThrowingLoadToItsWaitersAndLoadsAgainAfter)
        {
            SharedCache cache;
            std::atomic<int> loads = 0;
            const auto failing = [&]() -> LoadedString {
                ++loads;
                EXPECT_TRUE(awaitWithin([&] { return cache.getStats().mCoalesced == 1; }));
                throw std::runtime_error("unreadable");
            };

            std::optional<std::string> waiterSaw;
            std::jthread loader([&] { EXPECT_THROW(cache.getOrLoad(42, failing), std::runtime_error); });
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
            loader.join();
            waiter.join();

            EXPECT_EQ(waiterSaw, "unreadable");
            EXPECT_EQ(loads, 1);
            EXPECT_EQ(cache.getRefFromObjectCacheOrNone(42), std::nullopt);
            EXPECT_EQ(
                *cache.getOrLoad(42, [] { return LoadedString{ std::make_shared<const std::string>("second") }; }),
                "second");
            EXPECT_EQ(cache.getStats().mLoaded, 1);
        }

        /// **Two keys load at once**: the load of one does not wait on the load of the other.
        TEST(ResourceGenericObjectCacheTest, getOrLoadLoadsTwoKeysInParallel)
        {
            SharedCache cache;
            std::atomic<int> started = 0;
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

        /// **A key loaded again after the cache let it go is a reload**, whichever way it went: removed, expired or
        /// cleared; a first load is none.
        TEST(ResourceGenericObjectCacheTest, aKeyLoadedAgainAfterItWentIsAReload)
        {
            SharedCache cache;
            const auto load = [] { return LoadedString{ std::make_shared<const std::string>("loaded") }; };
            cache.getOrLoad(1, load);
            cache.getOrLoad(2, load);
            cache.getOrLoad(3, load);
            EXPECT_EQ(cache.getStats().mReloaded, 0);

            // 1 removed, and 2 and 3 expired: stamped at the update's own time, past no delay.
            cache.removeFromObjectCache(1);
            cache.update(1.0, 0.0);
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
        TEST(ResourceGenericObjectCacheTest, pastItsBudgetTheLeastRecentlyUsedUnheldEntryGoesFirst)
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

            // The same three under a budget of 120 keep all three: the budget is what decides.
            CacheBudget roomy(120);
            SharedCache kept(roomy);
            for (const int key : { 1, 2, 3 })
                kept.getOrLoad(key, loadOf(40));
            EXPECT_EQ(kept.getStats().mSize, 3);
            EXPECT_EQ(kept.getStats().mExpired, 0);
        }

        /// **A hit moves an entry to the front**: 1 asked again after 2 was loaded makes 2 the one to go.
        TEST(ResourceGenericObjectCacheTest, aHitMovesAnEntryToTheFront)
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
        TEST(ResourceGenericObjectCacheTest, anEntryAnythingHoldsNeverGoes)
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
        TEST(ResourceGenericObjectCacheTest, anUpdateStampsWhatIsHeldAsUsed)
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
        TEST(ResourceGenericObjectCacheTest, oneBudgetKeepsOneOrderOverItsCaches)
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
        /// made smaller makes room at once.
        TEST(ResourceGenericObjectCacheTest, whatACacheLetsGoLeavesItsBudget)
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
            }
            EXPECT_EQ(budget.getHeld(), 0) << "the cache's own went with it";
        }

        /// **A cache and its budget count an entry in one step**: cleared on one thread while another loads, the
        /// budget is never told an entry went before it was told the entry came. Each of the 20,000 loads is a byte,
        /// so the budget holds 20,000 at the most, where one counted gone first wraps it round past 2^63.
        TEST(ResourceGenericObjectCacheTest, aClearBesideALoadNeverTakesFromTheBudgetWhatItWasNotGiven)
        {
            constexpr int sLoads = 20000;
            CacheBudget budget(CacheBudget::sUnbounded);
            SharedCache cache(budget);
            std::atomic<bool> loaded = false;
            std::jthread loader([&] {
                for (int key = 0; key < sLoads; ++key)
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

        /// **The budget is what the caches hold and what the system leaves beside them, less 1 GiB**: 8 GiB left and
        /// none held is 7 GiB; 8 left and 2 held, 9; half a GiB left and 3 held, 2.5, under what they hold, so they
        /// make room; under the reserve altogether, none; where the system states nothing, a budget nothing passes.
        TEST(ResourceCacheBudgetTest, theBudgetIsWhatTheCachesHoldAndWhatTheSystemLeavesLessTheReserve)
        {
            constexpr std::uint64_t gib = std::uint64_t{ 1 } << 30;
            EXPECT_EQ(CacheBudget::derive(8 * gib, 0), 7 * gib);
            EXPECT_EQ(CacheBudget::derive(8 * gib, 2 * gib), 9 * gib);
            EXPECT_EQ(CacheBudget::derive(gib / 2, 3 * gib), 5 * gib / 2);
            EXPECT_EQ(CacheBudget::derive(gib / 4, gib / 2), 0);
            EXPECT_EQ(CacheBudget::derive(std::nullopt, 2 * gib), CacheBudget::sUnbounded);
        }
    }
}
