#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <components/rtx/renderer/framedigest.hpp>

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

        const Rtx::DigestWords& getWords() const { return mWords; }

    private:
        Rtx::DigestWords mWords{};
    };

    /// Thirty-two hex digits, which is how a hashes file spells one and how `scene` reports one.
    std::string spellHash(const Rtx::DigestWords& words);
}
