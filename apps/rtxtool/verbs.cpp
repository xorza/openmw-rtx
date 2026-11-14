#include "verbs.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace RtxTool
{
    namespace
    {
        /// One command: the word it is typed as and what it does with the places it runs at.
        struct VerbRow
        {
            Verb mVerb;
            std::string_view mName;
            VerbPolicy mPolicy;
        };

        /// Every command, in the order `--help` prints them and the enum counts them.
        ///
        /// **The one list of the names**, which is what makes an option's ownership and the dispatch's table the same
        /// statement: a command renamed here is renamed in both. `view` flies nothing and freezes nothing, and keeps
        /// the game's interface, because somebody is flying it; `check` and `shot` fly a route unfrozen and stand
        /// still elsewhere; `bench` measures what moves; `noise` stands still, because a reference is many frames of
        /// one frame.
        constexpr std::array<VerbRow, static_cast<std::size_t>(Verb::Count)> sRows{ {
            { Verb::Info, "info", VerbPolicy{} },
            { Verb::Scene, "scene", VerbPolicy{ .mFreezes = true } },
            { Verb::Shot, "shot", VerbPolicy{ .mFreezes = true, .mFliesRoutes = true, .mHashes = true } },
            { Verb::View, "view", VerbPolicy{ .mPlayed = true } },
            { Verb::Bench, "bench", VerbPolicy{ .mFliesRoutes = true, .mMeasures = true } },
            { Verb::Check, "check", VerbPolicy{ .mFreezes = true, .mFliesRoutes = true } },
            { Verb::Film, "film", VerbPolicy{ .mFollowsTracks = true, .mMeasures = true } },
            { Verb::Noise, "noise", VerbPolicy{ .mFreezes = true } },
        } };

        /// The names, checked where they are declared: each command once, in enum order, no spelling shared.
        constexpr Rtx::NamedEnum sNames{ [] {
            std::array<std::pair<Verb, std::string_view>, sRows.size()> names{};
            for (std::size_t at = 0; at < sRows.size(); ++at)
                names[at] = { sRows[at].mVerb, sRows[at].mName };
            return names;
        }() };

        const VerbRow& rowOf(const Verb one)
        {
            assert(one != Verb::Count);
            return sRows[static_cast<std::size_t>(one)];
        }
    }

    const VerbPolicy& policyOf(const Verb one)
    {
        return rowOf(one).mPolicy;
    }

    std::string_view verbName(const Verb one)
    {
        return rowOf(one).mName;
    }

    std::optional<Verb> verbNamed(const std::string_view name)
    {
        return sNames.named(name);
    }

    std::string describeVerbs(const VerbSet set)
    {
        std::size_t left = set.size();

        std::string result;
        for (const VerbRow& row : sRows)
        {
            if (!set.contains(row.mVerb))
                continue;

            // The last of several is joined with "and", so a help line reads as a sentence rather
            // than as a list of flags.
            if (!result.empty())
                result += left == 1 ? " and " : ", ";

            result += '`';
            result += row.mName;
            result += '`';
            --left;
        }

        return result;
    }
}
