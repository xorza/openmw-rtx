#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

#include <components/rtx/contentkey.hpp>

namespace Rtx
{
    namespace
    {
        const std::array<std::uint32_t, 3> sRun{ 1, 2, 3 };

        ContentKey keyOf(std::span<const std::uint32_t> first, std::span<const std::uint32_t> second,
            std::string_view pass = "fold", std::uint32_t version = 1)
        {
            ContentDigest digest(pass, version);
            digest.add(first);
            digest.add(second);
            return digest.getKey();
        }

        /// One input is one key, and every part of what a key is made of moves it: any value of any
        /// run, the pass's name, and its version. A key that one of them did not move would file two
        /// inputs together, and a cache would hand the first one's output to the second.
        TEST(RtxContentKeyTest, oneInputIsOneKeyAndEveryPartOfItMovesTheKey)
        {
            const std::array<std::uint32_t, 2> tail{ 4, 5 };
            const ContentKey key = keyOf(sRun, tail);

            EXPECT_EQ(keyOf(sRun, tail), key);

            for (std::size_t at = 0; at < sRun.size(); ++at)
            {
                std::array<std::uint32_t, 3> moved = sRun;
                ++moved[at];
                EXPECT_NE(keyOf(moved, tail), key) << "value " << at << " of the first run";
            }

            std::array<std::uint32_t, 2> movedTail = tail;
            ++movedTail[1];
            EXPECT_NE(keyOf(sRun, movedTail), key) << "the second run";
            EXPECT_NE(keyOf(sRun, tail, "solid reach"), key) << "the pass";
            EXPECT_NE(keyOf(sRun, tail, "fold", 2), key) << "the version";
        }

        /// Where one run ends and the next begins is part of the input: `[1 2][3]` and `[1][2 3]`
        /// are the same twelve bytes, and each run's length before it is what keys them apart.
        TEST(RtxContentKeyTest, whereOneRunEndsIsPartOfTheKey)
        {
            const std::span<const std::uint32_t> all(sRun);
            EXPECT_NE(keyOf(all.first(2), all.subspan(2)), keyOf(all.first(1), all.subspan(1)));
            EXPECT_NE(keyOf(all, {}), keyOf({}, all)) << "an empty run is a run";
        }

        /// A value is a run of one, and the bytes a key counts are the input's alone: the three
        /// values above are twelve bytes, and the pass's name and version and the lengths add none.
        TEST(RtxContentKeyTest, aValueIsARunOfOneAndOnlyTheInputIsCounted)
        {
            ContentDigest asValue("fold", 1);
            asValue.addValue(std::uint32_t{ 7 });

            const std::array<std::uint32_t, 1> seven{ 7 };
            ContentDigest asRun("fold", 1);
            asRun.add(std::span<const std::uint32_t>(seven));

            EXPECT_EQ(asValue.getKey(), asRun.getKey());
            EXPECT_EQ(asValue.getBytes(), 4u);

            ContentDigest three("a pass with a long name", 1);
            three.add(std::span<const std::uint32_t>(sRun));
            EXPECT_EQ(three.getBytes(), 12u);
        }
    }
}
