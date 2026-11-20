#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/rtx/world/weather.hpp>

#include "benchrun.hpp"
#include "exitstatus.hpp"
#include "expectations.hpp"
#include "findingkind.hpp"
#include "measurewindow.hpp"

namespace RtxTool
{
    /// What a kind is called on the line (`--fail-on`) and in the report.
    std::string_view findingName(FindingKind kind);

    /// The kind called `name`, or nothing.
    std::optional<FindingKind> findingNamed(std::string_view name);

    /// Whether `kind` fails the run on its own.
    bool isFault(FindingKind kind);

    /// Which of its meetings a finding of a kind is placed at.
    enum class FindingPlaced : std::uint8_t
    {
        /// The first: a fault or a line of the log, met where it began.
        First,

        /// The worst, by its figure: a hitch or a leg's slow frames, stood at again where it cost most.
        Worst,
    };

    /// Where a finding of `kind` is placed.
    FindingPlaced placedOf(FindingKind kind);

    /// The sky a finding was met under, as it crossed from one weather to the next.
    struct FindingSky
    {
        Rtx::Weather mLeft = Rtx::Weather::Clear;
        Rtx::Weather mArriving = Rtx::Weather::Clear;
        float mCrossed = 0.0f;
    };

    /// Where a finding was met: the leg's stop, the leg's frame, the place as a stop that stands there
    /// (`describeBlock`) with its hour, and the sky. A line logged on another thread is placed at its leg and frame
    /// alone, with no eye, hour or sky.
    struct FindingPlace
    {
        std::string mLeg;
        std::uint32_t mFrame = 0;
        Stop mStood{};
        std::optional<FindingSky> mSky{};
    };

    /// One finding: its kind, what it is, where it is placed (`placedOf`) and what it came to there where the
    /// detail does not say (a hitch's figures), how often, and the worst figure it came with.
    struct Finding
    {
        FindingKind mKind = FindingKind::Count;
        std::string mDetail;
        FindingPlace mPlace;
        std::string mNote;
        std::uint32_t mCount = 0;
        double mWorstMs = 0.0;

        /// Whether the content is known to meet it (`Expectations`), which fails no run.
        bool mExpected = false;
    };

    /// What one meeting of a finding came to: its figure, how many times it is counted, and what it came to where
    /// the finding is placed at it.
    struct FindingMet
    {
        double mMs = 0.0;
        std::uint32_t mTimes = 1;
        std::string_view mNote{};
    };

    /// The findings of a run, the same kind and detail counted once at its place, at most `sMost` faults and
    /// `sMost` costs and marks: past either a finding is counted as dropped, by kind, so a content whose every
    /// frame finds something holds the run's memory to a bound and still says how many. A cost never takes a
    /// fault's room, and a cost the line fails on fails the run dropped as it does held.
    class FindingTable
    {
    public:
        static constexpr std::size_t sMost = 2048;

        FindingTable() = default;

        /// A table that holds what `expected` names as the content's known findings, which fail no run.
        explicit FindingTable(Expectations expected)
            : mExpected(std::move(expected))
        {
        }

        /// Whether counting a finding of `kind` and `detail` with figure `ms` takes its place, which a caller asks
        /// before it takes one: one not held with room for it, or one placed at its worst that `ms` is worse than.
        bool takesPlace(FindingKind kind, std::string_view detail, double ms = 0.0) const;

        /// Counts a finding of `kind` and `detail` `met.mTimes` more, at `place` and with `met.mNote` where it takes
        /// its place, or as dropped where there is no room for it.
        void add(FindingKind kind, std::string_view detail, const FindingPlace& place, const FindingMet& met = {});

        /// Counts one more of a finding that does not take its place (`takesPlace`), its figure the worse of the
        /// two: one held, or one dropped.
        void count(FindingKind kind, std::string_view detail, double ms = 0.0);

        std::span<const Finding> getFindings() const { return mFindings; }

        /// How many findings of `kind` were met past the bound.
        std::uint32_t getDropped(FindingKind kind) const { return mDropped[static_cast<std::size_t>(kind)]; }

        /// Failed where a fault was found or a cost `failOn` names, held or dropped, that is not expected, else passed.
        ExitStatus statusUnder(std::span<const FindingKind> failOn) const;

        /// What the content is known to meet that this run never met: an expectation gone stale, which the report
        /// names so its file is kept to what the content still does.
        std::vector<const Expectation*> getUnmet() const;

    private:
        const Finding* find(FindingKind kind, std::string_view detail) const;
        Finding* find(FindingKind kind, std::string_view detail);
        bool hasRoom(FindingKind kind) const;

        Expectations mExpected;
        std::vector<Finding> mFindings;
        std::size_t mFaults = 0;
        std::array<std::uint32_t, static_cast<std::size_t>(FindingKind::Count)> mDropped{};
    };

    /// Refills `into` with the detail a hitch in `spend` of `leg` is counted under: the leg and the stretch it spent
    /// most in (`Rtx::leadingStretch`), as "led by update on leg-3". Into a buffer kept, so a hitch counted again
    /// builds no string.
    void describeHitchKey(std::string& into, std::string_view leg, const Rtx::FrameSpend& spend);

    /// What the hitch `span` came to, where it is placed: its frame after `lastMs`, the `loaded` cells the game made
    /// active on it, the meshes that arrived and where the time went.
    std::string describeHitch(const ClosedSpan& span, double lastMs, std::size_t loaded);

    /// What a soak's two passes are held to each other by: what the place has in use — the content's vertices and
    /// indices, the instances placed, the structures' live bytes and the textures something names — and the median
    /// frame. Never what the renderer holds room for: a table keeps the room the flight's peak took (`PlacementTable`'s
    /// slots, the content's run ends), and the device the textures that rest (`TextureTable`), which the closing pass
    /// inherits from the whole tour and which no leak is. The textures by their count, which the scene and the
    /// content both state exactly, where the bytes of a texture that rests are the host's figure and not the device's.
    struct SoakFigures
    {
        std::uint32_t mVerticesUsed = 0;
        std::uint32_t mIndicesUsed = 0;
        std::uint32_t mInstances = 0;
        std::uint64_t mStructureLiveBytes = 0;
        std::uint32_t mTextures = 0;
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
    /// and detail once with how often and where it is placed, the block a `view`, a `shot` or a `bench` stands at
    /// under it; and the counts past the table's bound, by kind.
    std::string describeFindings(const FindingTable& table);

    /// How many findings a table holds of each standing in the report: the faults, the costs and marks, and those
    /// the content is known to meet.
    struct FindingCounts
    {
        std::size_t mFaults = 0;
        std::size_t mCosts = 0;
        std::size_t mExpected = 0;

        bool operator==(const FindingCounts& other) const = default;
    };
    FindingCounts countFindings(const FindingTable& table);

    /// The counts as a line of the run's record (`recordHeaderLine`), `{"findings": …}`: what a tour's driver reads,
    /// where it read the report's wording. A tour adds one after every leg, and the last stands.
    std::string recordFindingsLine(const FindingCounts& counts);
}
