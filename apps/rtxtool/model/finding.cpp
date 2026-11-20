#include "finding.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/world/weather.hpp>

#include "benchrecord.hpp"
#include "stopdescription.hpp"

namespace RtxTool
{
    namespace
    {
        struct KindRow
        {
            FindingKind mKind;
            std::string_view mName;
            bool mFault;
            FindingPlaced mPlaced;
        };

        constexpr std::array<KindRow, static_cast<std::size_t>(FindingKind::Count)> sKinds{ {
            { FindingKind::NotFinite, "not-finite", true, FindingPlaced::First },
            { FindingKind::Refused, "refused", true, FindingPlaced::First },
            { FindingKind::CheckFailed, "check", true, FindingPlaced::First },
            { FindingKind::LogError, "log-error", true, FindingPlaced::First },
            { FindingKind::Outrun, "outrun", true, FindingPlaced::First },
            { FindingKind::LegFailed, "leg-failed", true, FindingPlaced::First },
            { FindingKind::Hitch, "hitch", false, FindingPlaced::Worst },
            { FindingKind::SlowFrames, "slow-frames", false, FindingPlaced::Worst },
            { FindingKind::LogWarning, "log-warning", false, FindingPlaced::First },
            { FindingKind::Growth, "growth", false, FindingPlaced::First },
            { FindingKind::Mark, "mark", false, FindingPlaced::First },
        } };

        static_assert(
            [] {
                for (std::size_t at = 0; at < sKinds.size(); ++at)
                    if (static_cast<std::size_t>(sKinds[at].mKind) != at)
                        return false;
                return true;
            }(),
            "a finding kind out of the enum's order");

        const KindRow& rowOf(const FindingKind kind)
        {
            assert(kind != FindingKind::Count);
            return sKinds[static_cast<std::size_t>(kind)];
        }

        std::string describeSky(const FindingSky& sky)
        {
            if (sky.mLeft == sky.mArriving)
                return std::string(Rtx::nameOf(sky.mLeft));
            return std::format(
                "{} to {} {:.0f}%", Rtx::nameOf(sky.mLeft), Rtx::nameOf(sky.mArriving), 100.0f * sky.mCrossed);
        }

        /// Where `place` stands, as much of it as is known: the leg and its frame, the day and the hour, the sky.
        std::string describePlace(const FindingPlace& place)
        {
            std::string text = std::format("{} frame {}", place.mLeg, place.mFrame);
            if (const std::optional<float> hour = place.mStood.mSky.mHour)
                text += std::format(", day {} {}", place.mStood.mSky.mDay.value_or(0) + 1, describeHour(*hour));
            if (place.mSky.has_value())
                text += std::format(" under {}", describeSky(*place.mSky));
            return text;
        }
    }

    std::string_view findingName(const FindingKind kind)
    {
        return rowOf(kind).mName;
    }

    std::optional<FindingKind> findingNamed(const std::string_view name)
    {
        for (const KindRow& row : sKinds)
            if (row.mName == name)
                return row.mKind;
        return std::nullopt;
    }

    bool isFault(const FindingKind kind)
    {
        return rowOf(kind).mFault;
    }

    FindingPlaced placedOf(const FindingKind kind)
    {
        return rowOf(kind).mPlaced;
    }

    const Finding* FindingTable::find(const FindingKind kind, const std::string_view detail) const
    {
        const auto found = std::ranges::find_if(
            mFindings, [&](const Finding& finding) { return finding.mKind == kind && finding.mDetail == detail; });
        return found == mFindings.end() ? nullptr : &*found;
    }

    Finding* FindingTable::find(const FindingKind kind, const std::string_view detail)
    {
        // The one search, for both: the table is this object's own, so what it finds may be changed.
        return const_cast<Finding*>(std::as_const(*this).find(kind, detail));
    }

    bool FindingTable::hasRoom(const FindingKind kind) const
    {
        const std::size_t held = isFault(kind) ? mFaults : mFindings.size() - mFaults;
        return held < sMost;
    }

    bool FindingTable::takesPlace(const FindingKind kind, const std::string_view detail, const double ms) const
    {
        if (const Finding* held = find(kind, detail))
            return placedOf(kind) == FindingPlaced::Worst && ms > held->mWorstMs;
        return hasRoom(kind);
    }

    void FindingTable::add(
        const FindingKind kind, const std::string_view detail, const FindingPlace& place, const FindingMet& met)
    {
        if (Finding* held = find(kind, detail))
        {
            held->mCount += met.mTimes;
            if (placedOf(kind) == FindingPlaced::Worst && met.mMs > held->mWorstMs)
            {
                held->mPlace = place;
                held->mNote = met.mNote;
            }
            held->mWorstMs = std::max(held->mWorstMs, met.mMs);
            return;
        }
        if (!hasRoom(kind))
        {
            mDropped[static_cast<std::size_t>(kind)] += met.mTimes;
            return;
        }
        mFaults += isFault(kind) ? 1 : 0;
        mFindings.push_back(Finding{ .mKind = kind,
            .mDetail = std::string(detail),
            .mPlace = place,
            .mNote = std::string(met.mNote),
            .mCount = met.mTimes,
            .mWorstMs = met.mMs,
            .mExpected = mExpected.expects(kind, detail) });
    }

    void FindingTable::count(const FindingKind kind, const std::string_view detail, const double ms)
    {
        assert(!takesPlace(kind, detail, ms) && "a finding counted with no place where it takes one");
        if (Finding* held = find(kind, detail))
        {
            ++held->mCount;
            held->mWorstMs = std::max(held->mWorstMs, ms);
            return;
        }
        ++mDropped[static_cast<std::size_t>(kind)];
    }

    ExitStatus FindingTable::statusUnder(const std::span<const FindingKind> failOn) const
    {
        for (const KindRow& row : sKinds)
        {
            const bool fails = row.mFault || std::ranges::find(failOn, row.mKind) != failOn.end();
            const bool met = getDropped(row.mKind) > 0 || std::ranges::any_of(mFindings, [&](const Finding& finding) {
                return finding.mKind == row.mKind && !finding.mExpected;
            });
            if (fails && met)
                return ExitStatus::Failed;
        }
        return ExitStatus::Passed;
    }

    std::vector<const Expectation*> FindingTable::getUnmet() const
    {
        std::vector<const Expectation*> unmet;
        for (const Expectation& expected : mExpected.getEntries())
            if (find(expected.mKind, expected.mDetail) == nullptr)
                unmet.push_back(&expected);
        return unmet;
    }

    void describeHitchKey(std::string& into, const std::string_view leg, const Rtx::FrameSpend& spend)
    {
        into.clear();
        std::format_to(
            std::back_inserter(into), "led by {} on {}", Rtx::sTimings.name(Rtx::leadingStretch(spend)), leg);
    }

    std::string describeHitch(const ClosedSpan& span, const double lastMs, const std::size_t loaded)
    {
        return std::format("{:.1f} ms after {:.1f}{}: {} meshes arrived, {}", span.mSpend.at(Rtx::Timing::Frame),
            lastMs, loaded > 0 ? std::format(", the game loading {} cells", loaded) : std::string(), span.mArrived,
            Rtx::describeStretches(span.mSpend));
    }

    SoakFigures SoakFigures::of(const BenchPlace& place)
    {
        return SoakFigures{ .mVerticesUsed = place.mContent.mVerticesUsed,
            .mIndicesUsed = place.mContent.mIndicesUsed,
            .mInstances = place.mScene.mInstances.mPlaced,
            .mStructureLiveBytes = place.mScene.mStructureLiveBytes,
            .mTextures
            = place.mScene.mTextureCount - std::min(place.mScene.mTextureCount, place.mContent.mRestingTextures),
            .mMedianMs = place.at(Rtx::Timing::Frame).mMedian };
    }

    std::optional<std::string> describeGrowth(const SoakFigures& first, const SoakFigures& again)
    {
        const auto grew = [](const double before, const double after) { return after > before * (1.0 + sGrowthShare); };
        if (!grew(first.mVerticesUsed, again.mVerticesUsed) && !grew(first.mIndicesUsed, again.mIndicesUsed)
            && !grew(first.mInstances, again.mInstances)
            && !grew(static_cast<double>(first.mStructureLiveBytes), static_cast<double>(again.mStructureLiveBytes))
            && !grew(first.mTextures, again.mTextures) && !grew(first.mMedianMs, again.mMedianMs))
            return std::nullopt;

        constexpr double mebibyte = 1024.0 * 1024.0;
        return std::format(
            "the first leg again came to {} vertices in use against {}, {} indices against {}, {} instances against "
            "{}, {:.1f} MiB of live structures against {:.1f}, {} textures named against {}, and a {:.2f} ms "
            "median frame against {:.2f}",
            again.mVerticesUsed, first.mVerticesUsed, again.mIndicesUsed, first.mIndicesUsed, again.mInstances,
            first.mInstances, static_cast<double>(again.mStructureLiveBytes) / mebibyte,
            static_cast<double>(first.mStructureLiveBytes) / mebibyte, again.mTextures, first.mTextures,
            again.mMedianMs, first.mMedianMs);
    }

    namespace
    {
        /// What a finding is to the report: a fault, a cost, or one the content is known to meet, in that order.
        std::int32_t rankOf(const Finding& finding)
        {
            if (finding.mExpected)
                return 2;
            return isFault(finding.mKind) ? 0 : 1;
        }
    }

    FindingCounts countFindings(const FindingTable& table)
    {
        FindingCounts counts;
        for (const Finding& finding : table.getFindings())
        {
            const std::int32_t rank = rankOf(finding);
            counts.mFaults += rank == 0 ? 1 : 0;
            counts.mCosts += rank == 1 ? 1 : 0;
            counts.mExpected += rank == 2 ? 1 : 0;
        }
        return counts;
    }

    std::string recordFindingsLine(const FindingCounts& counts)
    {
        return std::format(R"({{"findings": {{"faults": {}, "costs": {}, "expected": {}}}}})"
                           "\n",
            counts.mFaults, counts.mCosts, counts.mExpected);
    }

    std::string describeFindings(const FindingTable& table)
    {
        const auto rank = [](const Finding& finding) { return rankOf(finding); };

        std::vector<const Finding*> ordered;
        for (const Finding& finding : table.getFindings())
            ordered.push_back(&finding);
        // Faults first, by kind; then the costs, the worst first within a kind; then the expected.
        std::ranges::stable_sort(ordered, [&](const Finding* a, const Finding* b) {
            if (rank(*a) != rank(*b))
                return rank(*a) < rank(*b);
            if (a->mKind != b->mKind)
                return a->mKind < b->mKind;
            return a->mWorstMs > b->mWorstMs;
        });

        const FindingCounts counts = countFindings(table);
        const std::vector<const Expectation*> unmet = table.getUnmet();

        std::string dropped;
        for (const KindRow& row : sKinds)
            if (const std::uint32_t count = table.getDropped(row.mKind); count > 0)
                dropped
                    += std::format("{} {} {}", dropped.empty() ? ", past the table's bound:" : ",", count, row.mName);

        std::string text = std::format("\ntour findings: {} faults, {} costs and marks, {} expected{}\n",
            counts.mFaults, counts.mCosts, counts.mExpected, dropped);
        for (const Expectation* stale : unmet)
            text += std::format("\nUNMET [{}] {}: {}\n  expected and never met\n", stale->mName,
                findingName(stale->mKind), stale->mDetail);
        constexpr std::array<std::string_view, 3> sRanks{ "FAULT", "cost ", "known" };
        for (const Finding* finding : ordered)
        {
            const FindingPlace& place = finding->mPlace;
            text += std::format("\n{} {}: {}{}{}\n  {} at {}\n", sRanks.at(static_cast<std::size_t>(rank(*finding))),
                findingName(finding->mKind), finding->mDetail,
                finding->mCount > 1 ? std::format(", {} times", finding->mCount) : "",
                finding->mWorstMs > 0.0 ? std::format(", worst {:.1f} ms", finding->mWorstMs) : "",
                placedOf(finding->mKind) == FindingPlaced::First ? "first" : "worst", describePlace(place));
            if (!finding->mNote.empty())
                text += std::format("  {}\n", finding->mNote);
            if (place.mStood.mStand.mEye.has_value())
                text += describeBlock(place.mStood);
        }
        return text;
    }
}
