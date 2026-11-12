#include "framepast.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Rtx
{
    namespace
    {
        /// Each item of `items` that `has`, joined as a sentence lists them: "a, b and c".
        template <std::size_t N>
        std::string listed(const std::array<std::pair<bool, std::string_view>, N>& items)
        {
            std::string said;
            std::size_t left = 0;
            for (const auto& [has, name] : items)
                left += has ? 1 : 0;
            for (const auto& [has, name] : items)
            {
                if (!has)
                    continue;
                --left;
                said += name;
                said += left > 1 ? ", " : left == 1 ? " and " : "";
            }
            return said;
        }
    }

    std::string describe(const FramePast& past)
    {
        if (!past.losesAny())
            return {};

        const std::string causes = listed(std::array{
            std::pair{ (past.mCauses & FramePast::Cut) != 0, std::string_view("a cut") },
            std::pair{ (past.mCauses & FramePast::Worldspace) != 0, std::string_view("another worldspace") },
            std::pair{ (past.mCauses & FramePast::Resized) != 0, std::string_view("a new extent") },
            std::pair{ (past.mCauses & FramePast::NewWorld) != 0, std::string_view("a new world") },
        });
        return (causes.empty() ? std::string("a loss nobody named") : causes) + ": "
            + listed(std::array{
                std::pair{ past.mReprojectionLost, std::string_view("the reprojection") },
                std::pair{ past.mEyeLost, std::string_view("the eye") },
                std::pair{ past.mWaterLost, std::string_view("the wake") },
            });
    }
}
