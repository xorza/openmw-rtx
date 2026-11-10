#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include <components/misc/digestwords.hpp>
#include <components/misc/hashstate.hpp>

namespace ContentStore
{
    /// What an entry is filed under: the name and version of what made it and every byte it was made
    /// from, as one 128-bit `MurmurHash3_x64_128`. A store has to hold some 2^64 keys before two inputs
    /// are likely to share one, and every model and texture a game ships is under 2^20.
    struct Key
    {
        Misc::DigestWords mHash{};

        bool operator==(const Key& other) const = default;
    };

    /// A key's hash for a table: its first word, since a key is a hash already.
    struct KeyHash
    {
        std::size_t operator()(const Key& key) const noexcept { return static_cast<std::size_t>(key.mHash[0]); }
    };

    /// Builds a `Key` from what an entry is made from, one run of values at a time.
    ///
    /// **Each run goes in behind its length**, so that where one run ends and the next begins is
    /// part of the key: `[a b][c]` and `[a][b c]` are two inputs, and without the lengths they are
    /// one stream of bytes.
    class Digest
    {
    public:
        /// Starts a key for what `maker` names at `version`, which a maker whose output changed for the
        /// same input moves up.
        Digest(std::string_view maker, std::uint32_t version);

        /// Adds a run of values, bytes as they stand. `T` carries no padding, whose bytes are
        /// whatever the memory held and would make one input two keys.
        template <class T>
        requires std::is_trivially_copyable_v<T>
        void add(std::span<const T> values) { addBytes(std::as_bytes(values)); }

        /// Adds one value, as a run of one. Never a span, by `Misc::HashState::add`'s guard: a span is
        /// trivially copyable too, and taken as a value it would key its address and not what it
        /// spans.
        template <class T>
        requires(std::is_trivially_copyable_v<T> && !Misc::sIsSpan<T>) void add(const T& value)
        {
            add(std::span<const T>(&value, 1));
        }

        Key getKey() const { return Key{ mState.getWords() }; }

        /// How many bytes of input went into the key, lengths left out: what a report weighs the
        /// time the key took against.
        std::uint64_t getBytes() const { return mBytes; }

    private:
        void addBytes(std::span<const std::byte> bytes);

        Misc::HashState mState;
        std::uint64_t mBytes = 0;
    };
}
