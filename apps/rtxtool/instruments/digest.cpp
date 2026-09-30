#include "digest.hpp"

#include <format>

#include <smhasher/MurmurHash3.h>

namespace RtxTool
{
    void Digest::add(std::span<const std::byte> bytes)
    {
        // The seed is read whole before anything is written, but a copy costs two words and makes
        // that true whatever the implementation does.
        const std::array<std::uint64_t, 2> seed = mWords;
        MurmurHash3_x64_128(bytes.data(), static_cast<int>(bytes.size()), seed.data(), mWords.data());
    }

    std::string spellHash(const std::array<std::uint64_t, 2>& words)
    {
        return std::format("{:016x}{:016x}", words[0], words[1]);
    }
}
