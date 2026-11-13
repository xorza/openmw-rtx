#include "framespend.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <format>

namespace Rtx
{
    namespace
    {
        /// At namespace scope, one object: MSVC captures no `constexpr` local a lambda names, and each use of one
        /// there is a copy of its own, so a `find` in one copy never met the other copy's end and every timing
        /// read as no stretch.
        constexpr std::array sNotStretches{ Timing::Frame, Timing::Wait, Timing::Preprocess, Timing::Bake,
            Timing::Textures, Timing::Upload, Timing::Sleep };
    }

    std::string describeStretches(const FrameSpend& spend)
    {
        std::array<Timing, sTimingCount> stretches = sTimings.values();
        const auto end = std::remove_if(stretches.begin(), stretches.end(),
            [](const Timing timing) { return std::ranges::find(sNotStretches, timing) != sNotStretches.end(); });

        constexpr std::size_t sShown = 3;
        assert(end - stretches.begin() >= static_cast<std::ptrdiff_t>(sShown) && "fewer stretches than are shown");
        std::partial_sort(stretches.begin(), stretches.begin() + sShown, end, [&](const Timing a, const Timing b) {
            return spend.at(a) != spend.at(b) ? spend.at(a) > spend.at(b) : indexOf(a) < indexOf(b);
        });

        std::string described;
        for (std::size_t at = 0; at < sShown; ++at)
            described += std::format(
                "{}{} {:.1f}", at == 0 ? "" : " ", sTimings.name(stretches[at]), spend.at(stretches[at]));
        return described;
    }
}
