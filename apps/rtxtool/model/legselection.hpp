#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <components/misc/result.hpp>

#include "tour.hpp"

namespace RtxTool
{
    /// **Every how many land legs and rooms a tour flies where the line names none** (`tour --every`): a sample of
    /// both, in about a quarter of the time, whose legs' p99 median stood within 3.1 ms and 12% of the whole tour's
    /// over twelve tours of the handheld's and Windows' contents, and which a whole tour's same legs, by name, hold
    /// exactly. A release's verdict, a fault's hunt and the rest after a crash fly every leg, `--every=1`.
    inline constexpr std::uint32_t sEveryLeg = 4;

    /// **Which legs of a tour fly** (`tour --legs`, `--every`): kinds of leg, and legs and runs of them by the numbers
    /// `--plan` gives them, all together and in the plan's order; then every `every`th land leg of those and every
    /// `every`th room, the first of each among them, and the closing leg where it is named.
    /// What a question asks of a tour flies and nothing else — a fix verified at the places it concerns, an A/B of the
    /// rooms alone — while the release's verdict flies every leg. A leg flown stands as the whole tour stood it
    /// (`stopsFor`), so its figures are held against the same leg of a whole tour by its name.
    class LegSelection
    {
    public:
        /// Every leg.
        LegSelection() = default;

        /// What `--legs` and `--every` name: `legs` a comma-separated list of `land`, `rooms`, `closing`, a leg's
        /// number counted from one, `a-b` the legs from a to b, and `a-` the legs from a on; nothing for every leg.
        /// Refused, saying why, where an item is none of those, a run ends before it begins, a leg is nought, or
        /// `every` is.
        static Result<LegSelection, std::string> parse(std::string_view legs, std::uint32_t every);

        /// The plan's legs it picks, their numbers from nought, in the plan's order. Refused where it names a leg past
        /// the plan's last.
        Result<std::vector<std::size_t>, std::string> pick(const TourPlan& plan) const;

    private:
        /// Legs from `mFirst` to `mLast`, counted from one, or to the plan's last where `mLast` is nothing.
        struct Run
        {
            std::size_t mFirst = 1;
            std::optional<std::size_t> mLast;
        };

        std::vector<LegKind> mKinds;
        std::vector<Run> mRuns;
        std::uint32_t mEvery = 1;
    };
}
