#include "extensionfilter.hpp"

#include <algorithm>

namespace Rtx
{
    std::vector<std::string> filterOfferedExtensions(
        const std::span<const std::string> requested, const std::span<const std::string_view> offered)
    {
        std::vector<std::string> kept;
        kept.reserve(requested.size());
        for (const std::string& name : requested)
            if (std::any_of(offered.begin(), offered.end(),
                    [&](const std::string_view candidate) { return name == candidate; }))
                kept.push_back(name);
        return kept;
    }
}
