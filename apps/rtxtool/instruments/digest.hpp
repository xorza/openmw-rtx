#pragma once

#include <string>

#include <components/misc/digestwords.hpp>

namespace RtxTool
{
    /// Thirty-two hex digits, which is how a hashes file spells one and how `scene` reports one.
    std::string spellHash(const Misc::DigestWords& words);
}
