#include "legselection.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace RtxTool
{
    namespace
    {
        /// `text` as a whole leg's number, or nothing where it is not one.
        std::optional<std::size_t> legNumber(const std::string_view text)
        {
            std::size_t number = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (error != std::errc{} || end != text.data() + text.size() || text.empty())
                return std::nullopt;
            return number;
        }

        std::optional<LegKind> legKindNamed(const std::string_view name)
        {
            if (name == "land")
                return LegKind::Land;
            if (name == "rooms")
                return LegKind::Room;
            if (name == "closing")
                return LegKind::Closing;
            return std::nullopt;
        }
    }

    Result<LegSelection, std::string> LegSelection::parse(const std::string_view legs, const std::uint32_t every)
    {
        if (every == 0)
            return Err{ std::string("--every=0 flies no leg: every leg is --every=1") };

        LegSelection selection;
        selection.mEvery = every;
        std::size_t at = 0;
        while (at < legs.size())
        {
            const std::size_t comma = std::min(legs.find(',', at), legs.size());
            const std::string_view item = legs.substr(at, comma - at);
            at = comma + 1;

            if (const std::optional<LegKind> kind = legKindNamed(item); kind.has_value())
            {
                selection.mKinds.push_back(*kind);
                continue;
            }

            const std::size_t dash = item.find('-');
            const std::optional<std::size_t> first = legNumber(item.substr(0, dash));
            const std::string_view after
                = dash == std::string_view::npos ? item : item.substr(std::min(dash + 1, item.size()));
            const std::optional<std::size_t> last
                = dash == std::string_view::npos ? first : (after.empty() ? std::nullopt : legNumber(after));
            if (!first.has_value() || (dash != std::string_view::npos && !after.empty() && !last.has_value()))
                return Err{ std::format(
                    "--legs names \"{}\", which is no land, rooms or closing, no leg's number and no run of them",
                    item) };
            if (*first == 0)
                return Err{ std::format("--legs names leg 0 in \"{}\": a tour's legs are counted from one", item) };
            if (last.has_value() && *last < *first)
                return Err{ std::format("--legs names \"{}\", a run that ends before it begins", item) };
            selection.mRuns.push_back(Run{ .mFirst = *first, .mLast = last });
        }
        return selection;
    }

    Result<std::vector<std::size_t>, std::string> LegSelection::pick(const TourPlan& plan) const
    {
        const std::size_t legs = plan.mLegs.size();
        for (const Run& run : mRuns)
            if (std::max(run.mFirst, run.mLast.value_or(run.mFirst)) > legs)
                return Err{ std::format("--legs names leg {}, past the tour's last, {}",
                    std::max(run.mFirst, run.mLast.value_or(run.mFirst)), legs) };

        const bool everyLeg = mKinds.empty() && mRuns.empty();
        std::vector<std::size_t> picked;
        std::size_t named = 0;
        for (std::size_t number = 0; number < legs; ++number)
        {
            const bool kinded = std::ranges::find(mKinds, plan.mLegs[number].mKind) != mKinds.end();
            const bool ranged = std::ranges::any_of(mRuns,
                [&](const Run& run) { return number + 1 >= run.mFirst && number + 1 <= run.mLast.value_or(legs); });
            if (!everyLeg && !kinded && !ranged)
                continue;
            if (named++ % mEvery == 0)
                picked.push_back(number);
        }
        return picked;
    }
}
