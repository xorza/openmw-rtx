#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include <components/rtx/common/digestwords.hpp>
#include <components/rtx/common/hashstate.hpp>

namespace Rtx
{
    /// What a pass's output is filed under: the pass, its version and every byte of what it reads,
    /// as one 128-bit `MurmurHash3_x64_128`. A store has to hold some 2^64 keys before two inputs
    /// are likely to share one, and every model and texture a game ships is under 2^20.
    struct ContentKey
    {
        DigestWords mHash{};

        bool operator==(const ContentKey& other) const = default;
    };

    /// Builds a `ContentKey` from what a pass reads, one run of values at a time.
    ///
    /// **Each run goes in behind its length**, so that where one run ends and the next begins is
    /// part of the key: `[a b][c]` and `[a][b c]` are two inputs, and without the lengths they are
    /// one stream of bytes.
    class ContentDigest
    {
    public:
        /// Starts a key for `pass` at `version`, which a pass whose output changed for the same
        /// input moves up.
        ContentDigest(std::string_view pass, std::uint32_t version);

        /// Adds a run of values, bytes as they stand. `T` carries no padding, whose bytes are
        /// whatever the memory held and would make one input two keys.
        template <class T>
        requires std::is_trivially_copyable_v<T>
        void add(std::span<const T> values) { addBytes(std::as_bytes(values)); }

        /// Adds one value, as a run of one. Never a span, by `HashState::add`'s guard: a span is
        /// trivially copyable too, and taken as a value it would key its address and not what it
        /// spans.
        template <class T>
        requires(std::is_trivially_copyable_v<T> && !sIsSpan<T>) void add(const T& value)
        {
            add(std::span<const T>(&value, 1));
        }

        ContentKey getKey() const { return ContentKey{ mState.getWords() }; }

        /// How many bytes of input went into the key, lengths left out: what a report weighs the
        /// time the key took against.
        std::uint64_t getBytes() const { return mBytes; }

    private:
        void addBytes(std::span<const std::byte> bytes);

        HashState mState;
        std::uint64_t mBytes = 0;
    };
}
