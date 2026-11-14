#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <components/rtx/world/weather.hpp>

#include "benchrun.hpp"
#include "exitstatus.hpp"
#include "findingkind.hpp"

namespace RtxTool
{
    /// What a kind is called on the line (`--fail-on`) and in the report.
    std::string_view findingName(FindingKind kind);

    /// The kind called `name`, or nothing.
    std::optional<FindingKind> findingNamed(std::string_view name);

    /// Whether `kind` fails the run on its own.
    bool isFault(FindingKind kind);

    /// Where a finding was first met: the leg's stop, the leg's frame, the place as a stop that stands
    /// there (`describeBlock`), and the sky as it crossed.
    struct FindingPlace
    {
        std::string mLeg;
        std::uint32_t mFrame = 0;
        Stop mStood{};
        Rtx::Weather mLeft = Rtx::Weather::Clear;
        Rtx::Weather mArriving = Rtx::Weather::Clear;
        float mCrossed = 0.0f;
    };

    /// One finding: its kind, what it is, where it was first met, how often, and the worst figure it came with.
    struct Finding
    {
        FindingKind mKind = FindingKind::Count;
        std::string mDetail;
        FindingPlace mFirst;
        std::uint32_t mCount = 0;
        double mWorstMs = 0.0;
    };

    /// The findings of a run, the same kind and detail counted once with its first place, at most `sMost` of them:
    /// past it a finding is counted as dropped, so a content whose every frame finds something holds the run's
    /// memory to a bound and still says how many.
    class FindingTable
    {
    public:
        static constexpr std::size_t sMost = 2048;

        /// Whether a finding of `kind` and `detail` is held already, which a caller asks before it takes a place.
        bool holds(FindingKind kind, std::string_view detail) const;

        /// Counts a finding of `kind` and `detail` `times` more, at `place` where it is the first, with `ms` its
        /// figure.
        void add(FindingKind kind, std::string_view detail, const FindingPlace& place, double ms = 0.0,
            std::uint32_t times = 1);

        /// Counts one more of a finding held already, its figure the worse of the two. A finding not held is a
        /// contract broken: `holds` is asked first.
        void again(FindingKind kind, std::string_view detail, double ms = 0.0);

        std::span<const Finding> getFindings() const { return mFindings; }
        std::uint32_t getDropped() const { return mDropped; }

        /// Failed where a fault was found or a cost `failOn` names, else passed.
        ExitStatus statusUnder(std::span<const FindingKind> failOn) const;

    private:
        Finding* find(FindingKind kind, std::string_view detail);

        std::vector<Finding> mFindings;
        std::uint32_t mDropped = 0;
    };

    /// What a soak's two passes are held to each other by: the device's live memory, the content's vertices in use,
    /// and the median frame.
    struct SoakFigures
    {
        std::uint64_t mDeviceLive = 0;
        std::uint32_t mVerticesUsed = 0;
        double mMedianMs = 0.0;

        static SoakFigures of(const BenchPlace& place);
    };

    /// How far a figure may come back above where the first pass left it before it is growth: past a run's noise
    /// on the median and past the residency a texture's arrival moves, and short of a leak a soak is for.
    inline constexpr double sGrowthShare = 0.10;

    /// What the pass `again` came to past `first`, every figure beside its first, where one grew past
    /// `sGrowthShare`; or nothing.
    std::optional<std::string> describeGrowth(const SoakFigures& first, const SoakFigures& again);

    /// The findings as a person reads them after a tour: the faults first, then the costs worst first, each kind
    /// and detail once with how often and where it was first met, the block a `view`, a `shot` or a `bench` stands
    /// at under it; and the count past the table's bound.
    std::string describeFindings(const FindingTable& table);
}
