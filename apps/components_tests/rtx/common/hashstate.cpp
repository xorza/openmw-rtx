#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

#include <smhasher/MurmurHash3.h>

#include <components/rtx/common/digestwords.hpp>
#include <components/rtx/common/hashstate.hpp>

namespace Rtx
{
    namespace
    {
        template <class T>
        concept Hashable = requires(HashState state, const T& value)
        {
            state.add(value);
        };

        /// A value is hashed as its bytes, a run of values as theirs, and a span that is not a run
        /// of constants does not compile: taken as a value, it would hash its address.
        static_assert(Hashable<std::uint32_t>);
        static_assert(Hashable<DigestWords>);
        static_assert(Hashable<std::span<const std::uint32_t>>);
        static_assert(!Hashable<std::span<std::uint32_t>>);

        DigestWords murmur(std::string_view bytes, const DigestWords& seed)
        {
            DigestWords out{};
            MurmurHash3_x64_128(bytes.data(), static_cast<int>(bytes.size()), seed.data(), out.data());
            return out;
        }

        /// **The state is the seed of the next run**, which the content keys, the shaders' digest
        /// and the harness's digests were each written to: one run from nothing is the hash with a
        /// seed of nought, two are the second hashed with the first as its seed, and an empty run
        /// is a step too. Read against the hash called directly.
        TEST(RtxHashStateTest, eachRunIsHashedWithTheStateAsItsSeed)
        {
            constexpr std::string_view first = "visibility.rgen";
            constexpr std::string_view second = "a second run, longer than one sixteen-byte block";

            HashState none;
            EXPECT_EQ(none.getWords(), (DigestWords{ 0, 0 }));

            HashState one;
            one.add(std::span<const char>(first));
            EXPECT_EQ(one.getWords(), murmur(first, DigestWords{}));

            HashState two;
            two.add(std::span<const char>(first));
            two.add(std::span<const char>(second));
            EXPECT_EQ(two.getWords(), murmur(second, murmur(first, DigestWords{})));

            HashState emptied;
            emptied.add(std::span<const char>(first));
            emptied.add(std::span<const char>());
            EXPECT_EQ(emptied.getWords(), murmur({}, murmur(first, DigestWords{})));
            EXPECT_NE(emptied.getWords(), one.getWords()) << "an empty run left the state where it stood";

            // A value is its bytes, the same as a run of one.
            constexpr std::uint32_t value = 0x01020304u;
            HashState asValue;
            asValue.add(value);
            HashState asRun;
            asRun.add(std::span<const std::uint32_t>(&value, 1));
            EXPECT_EQ(asValue.getWords(), asRun.getWords());
            EXPECT_EQ(asValue.getWords(),
                murmur(std::string_view(reinterpret_cast<const char*>(&value), sizeof(value)), DigestWords{}));
        }
    }
}
