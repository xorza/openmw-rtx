#include "digest.hpp"

#include <format>

#include <smhasher/MurmurHash3.h>

namespace RtxTool
{
    void Digest::add(std::span<const std::byte> bytes)
    {
        // The seed is read whole before anything is written, but a copy costs two words and makes
        // that true whatever the implementation does.
        const Rtx::DigestWords seed = mWords;
        MurmurHash3_x64_128(bytes.data(), static_cast<int>(bytes.size()), seed.data(), mWords.data());
    }

    std::string spellHash(const Rtx::DigestWords& words)
    {
        return std::format("{:016x}{:016x}", words[0], words[1]);
    }
}
