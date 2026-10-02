#pragma once

#include <string>

#include <components/rtx/common/digestwords.hpp>

namespace RtxTool
{
    /// Thirty-two hex digits, which is how a hashes file spells one and how `scene` reports one.
    std::string spellHash(const Rtx::DigestWords& words);
}
