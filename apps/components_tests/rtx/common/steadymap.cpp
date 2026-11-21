#include <cstddef>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/common/maproom.hpp>
#include <components/rtx/common/steadymap.hpp>

namespace Rtx
{
    namespace
    {
        using Map = SteadyMap<std::uint64_t, std::uint64_t>;

        /// **A reservation is half the slots, and it holds under a world's churn**: room for 6400 makes 16,384 slots,
        /// 8192 entries before they double, and 400 rounds of letting every entry go one at a time and taking a new one
        /// in its place, at random keys as addresses are, leave that room as it was and never fill it. Boost's map,
        /// reserved the same, rehashed 1577 times over the same churn.
        TEST(RtxSteadyMapTest, aReservationHoldsUnderChurn)
        {
            Map map;
            map.reserve(6400);
            ASSERT_EQ(map.max_load(), 8192u);

            // The same sequence each run, on purpose.
            // NOLINTNEXTLINE(bugprone-random-generator-seed)
            std::mt19937_64 random(7);
            std::vector<std::uint64_t> live;
            for (std::uint32_t at = 0; at < 6400; ++at)
            {
                live.push_back(random());
                map.try_emplace(live.back(), at);
            }

            std::uint32_t filled = 0;
            std::size_t oldest = 0;
            for (std::uint32_t round = 0; round < 400; ++round)
                for (std::uint32_t at = 0; at < 6400; ++at)
                {
                    map.erase(map.find(live[oldest]));
                    live[oldest] = random();
                    filled += fillsRoom(map) ? 1u : 0u;
                    map.try_emplace(live[oldest], at);
                    oldest = (oldest + 1) % live.size();
                }
            EXPECT_EQ(filled, 0u);
            EXPECT_EQ(map.max_load(), 8192u);
            EXPECT_EQ(map.size(), 6400u);
        }

        /// **What it holds is what a standard map holds, through any order of inserts and erases**: 20,000 steps of
        /// random keys from a range of 512, so a key is met again, erased and taken back, past the room of 64 so the
        /// slots double, checked against `std::unordered_map` at every step by size and by every key.
        TEST(RtxSteadyMapTest, itHoldsWhatAStandardMapHolds)
        {
            Map map;
            map.reserve(64);
            std::unordered_map<std::uint64_t, std::uint64_t> model;
            // The same sequence each run, on purpose.
            // NOLINTNEXTLINE(bugprone-random-generator-seed)
            std::mt19937_64 random(11);
            for (std::uint32_t step = 0; step < 20000; ++step)
            {
                const std::uint64_t key = random() % 512;
                if (random() % 3 == 0)
                {
                    const auto found = map.find(key);
                    ASSERT_EQ(found != map.end(), model.contains(key)) << key;
                    if (found != map.end())
                        map.erase(found);
                    model.erase(key);
                }
                else
                {
                    const auto [entry, made] = map.try_emplace(key, step);
                    const auto [modelled, modelMade] = model.try_emplace(key, step);
                    ASSERT_EQ(made, modelMade) << key;
                    ASSERT_EQ(entry->second, modelled->second) << key;
                }
                ASSERT_EQ(map.size(), model.size());
            }
            const Map& held = map;
            for (const auto& [key, value] : model)
            {
                const auto found = held.find(key);
                ASSERT_NE(found, held.end()) << key;
                EXPECT_EQ(found->second, value) << key;
            }
            for (const auto& [key, value] : map)
                EXPECT_EQ(model.at(key), value) << key;
        }

        /// **A loop that erases as it goes visits every entry once**: an erase answers the entry moved into the hole,
        /// so of keys 0 to 99 a sweep dropping the even ones visits 100 and leaves the 50 odd ones.
        TEST(RtxSteadyMapTest, aSweepThatErasesAsItGoesVisitsEachEntryOnce)
        {
            Map map;
            map.reserve(100);
            for (std::uint64_t key = 0; key < 100; ++key)
                map.try_emplace(key, key);

            std::uint32_t visited = 0;
            for (auto entry = map.begin(); entry != map.end();)
            {
                ++visited;
                entry = entry->first % 2 == 0 ? map.erase(entry) : entry + 1;
            }
            EXPECT_EQ(visited, 100u);
            EXPECT_EQ(map.size(), 50u);
            for (std::uint64_t key = 0; key < 100; ++key)
                EXPECT_EQ(map.contains(key), key % 2 == 1) << key;

            EXPECT_EQ(map.erase_if([](const auto& entry) { return entry.first < 50; }), 25u);
            EXPECT_EQ(map.size(), 25u);
        }
    }
}
