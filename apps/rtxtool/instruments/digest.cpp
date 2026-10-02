#include "digest.hpp"

#include <format>

namespace RtxTool
{
    std::string spellHash(const Rtx::DigestWords& words)
    {
        return std::format("{:016x}{:016x}", words[0], words[1]);
    }
}
