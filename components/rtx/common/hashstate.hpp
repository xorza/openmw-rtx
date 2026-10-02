#pragma once

#include <cstddef>
#include <span>
#include <type_traits>

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
        void add(std::span<const std::byte> bytes);

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
