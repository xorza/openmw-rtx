#include "framespend.hpp"

#include <algorithm>
#include <format>

namespace Rtx
{
    std::string describeStretches(const FrameSpend& spend)
    {
        constexpr std::array sNotStretches{ Timing::Frame, Timing::Wait, Timing::Preprocess, Timing::Bake,
            Timing::Textures, Timing::Upload, Timing::Sleep };
        std::array<Timing, sTimingCount> stretches = sTimings.values();
        const auto end = std::remove_if(stretches.begin(), stretches.end(),
            [&](const Timing timing) { return std::ranges::find(sNotStretches, timing) != sNotStretches.end(); });

        constexpr std::size_t sShown = 3;
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
