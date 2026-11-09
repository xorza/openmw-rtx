#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rtx
{
    /// The names in `requested` that `offered` holds, kept in `requested`'s order. The device's own
    /// extension list, never the renderer's optional-extension list: NGX asks for names that list
    /// does not know.
    std::vector<std::string> filterOfferedExtensions(
        std::span<const std::string> requested, std::span<const std::string_view> offered);
}
