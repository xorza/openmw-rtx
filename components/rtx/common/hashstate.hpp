#pragma once

#include <algorithm>
#include <climits>
#include <cstddef>
#include <span>
#include <type_traits>

#include <smhasher/MurmurHash3.h>

#include "digestwords.hpp"

namespace Rtx
{
    template <class T>
    inline constexpr bool sIsSpan = false;

    template <class T, std::size_t Extent>
    inline constexpr bool sIsSpan<std::span<T, Extent>> = true;

    /// `MurmurHash3_x64_128` over whatever is added, in the order it is added, chained through its
    /// whole 128-bit state: each run is hashed with the state as its seed, so a digest of many runs
    /// needs no copy of them laid end to end, and nothing added early is narrowed on the way. The
    /// content keys, the shaders' digest, the SPIR-V digest and the harness all hash with this.
    class HashState
    {
    public:
        /// Defined here and not in a source file, because the SPIR-V tools hash with it and link
        /// nothing of the core.
        void add(std::span<const std::byte> bytes)
        {
            // The hash takes its length as an `int`, and a run past that is taken in pieces, each
            // chained into the state like any other. An empty run is still a step, so that it
            // moves the state as a run of any other length does. The seed is copied, so that the
            // output never overlaps what the hash reads, whatever its implementation does.
            do
            {
                const std::size_t piece = std::min<std::size_t>(bytes.size(), INT_MAX);
                const DigestWords seed = mWords;
                MurmurHash3_x64_128(bytes.data(), static_cast<int>(piece), seed.data(), mWords.data());
                bytes = bytes.subspan(piece);
            } while (!bytes.empty());
        }

        /// A run of values, bytes as they stand. `T` carries no padding, whose bytes are whatever
        /// the memory held and would make one input two digests.
        template <class T>
        requires std::is_trivially_copyable_v<T>
        void add(std::span<const T> values) { add(std::as_bytes(values)); }

        /// One value, as a run of one. Never a span: a span is trivially copyable too, and taken as
        /// a value it would hash its address and not what it spans.
        template <class T>
        requires(std::is_trivially_copyable_v<T> && !sIsSpan<T>) void add(const T& value)
        {
            add(std::span<const T>(&value, 1));
        }

        const DigestWords& getWords() const { return mWords; }

    private:
        DigestWords mWords{};
    };
}
