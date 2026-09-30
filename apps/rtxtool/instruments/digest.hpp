#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace RtxTool
{
    /// MurmurHash3 over whatever is fed to it, in the order it is fed. Chained through the seed, so
    /// a digest of many spans needs no copy of them laid end to end.
    class Digest
    {
    public:
        void add(std::span<const std::byte> bytes);

        template <class T>
        void add(std::span<const T> values)
        {
            add(std::as_bytes(values));
        }

        template <class T>
        void add(const T& value)
        {
            add(std::span<const T>(&value, 1));
        }

        const std::array<std::uint64_t, 2>& getWords() const { return mWords; }

    private:
        std::array<std::uint64_t, 2> mWords{};
    };

    /// Thirty-two hex digits, which is how a hashes file spells one and how `scene` reports one.
    std::string spellHash(const std::array<std::uint64_t, 2>& words);
}
