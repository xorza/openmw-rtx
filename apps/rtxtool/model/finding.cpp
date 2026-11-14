#include "finding.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
        };

        constexpr std::array<KindRow, static_cast<std::size_t>(FindingKind::Count)> sKinds{ {
            { FindingKind::NotFinite, "not-finite", true },
            { FindingKind::Refused, "refused", true },
            { FindingKind::CheckFailed, "check", true },
            { FindingKind::LogError, "log-error", true },
            { FindingKind::Outrun, "outrun", true },
            { FindingKind::LegFailed, "leg-failed", true },
            { FindingKind::Hitch, "hitch", false },
            { FindingKind::SlowFrames, "slow-frames", false },
            { FindingKind::LogWarning, "log-warning", false },
            { FindingKind::Growth, "growth", false },
            { FindingKind::Mark, "mark", false },
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

        std::string describeSky(const FindingPlace& place)
        {
            if (place.mLeft == place.mArriving)
                return std::string(Rtx::nameOf(place.mLeft));
            return std::format(
                "{} to {} {:.0f}%", Rtx::nameOf(place.mLeft), Rtx::nameOf(place.mArriving), 100.0f * place.mCrossed);
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

    Finding* FindingTable::find(const FindingKind kind, const std::string_view detail)
    {
        const auto found = std::ranges::find_if(
            mFindings, [&](const Finding& finding) { return finding.mKind == kind && finding.mDetail == detail; });
        return found == mFindings.end() ? nullptr : &*found;
    }

    bool FindingTable::holds(const FindingKind kind, const std::string_view detail) const
    {
        return std::ranges::any_of(
            mFindings, [&](const Finding& finding) { return finding.mKind == kind && finding.mDetail == detail; });
    }

    void FindingTable::add(const FindingKind kind, const std::string_view detail, const FindingPlace& place,
        const double ms, const std::uint32_t times)
    {
        if (Finding* held = find(kind, detail))
        {
            held->mCount += times;
            held->mWorstMs = std::max(held->mWorstMs, ms);
            return;
        }
        if (mFindings.size() == sMost)
        {
            mDropped += times;
            return;
        }
        mFindings.push_back(
            Finding{ .mKind = kind, .mDetail = std::string(detail), .mFirst = place, .mCount = times, .mWorstMs = ms });
    }

    void FindingTable::again(const FindingKind kind, const std::string_view detail, const double ms)
    {
        Finding* held = find(kind, detail);
        assert(held != nullptr && "a finding counted again that was never found");
        ++held->mCount;
        held->mWorstMs = std::max(held->mWorstMs, ms);
    }

    ExitStatus FindingTable::statusUnder(const std::span<const FindingKind> failOn) const
    {
        for (const Finding& finding : mFindings)
            if (isFault(finding.mKind) || std::ranges::find(failOn, finding.mKind) != failOn.end())
                return ExitStatus::Failed;
        return ExitStatus::Passed;
    }

    SoakFigures SoakFigures::of(const BenchPlace& place)
    {
        SoakFigures figures{ .mVerticesUsed = place.mContent.mVerticesUsed,
            .mMedianMs = place.at(Rtx::Timing::Frame).mMedian };
        for (std::uint32_t heap = 0; heap < place.mMemory.mHeapCount; ++heap)
            if (place.mMemory.mHeaps[heap].mDeviceLocal)
                figures.mDeviceLive += place.mMemory.mHeaps[heap].mLive;
        return figures;
    }

    std::optional<std::string> describeGrowth(const SoakFigures& first, const SoakFigures& again)
    {
        const auto grew = [](const double before, const double after) { return after > before * (1.0 + sGrowthShare); };
        if (!grew(static_cast<double>(first.mDeviceLive), static_cast<double>(again.mDeviceLive))
            && !grew(first.mVerticesUsed, again.mVerticesUsed) && !grew(first.mMedianMs, again.mMedianMs))
            return std::nullopt;

        constexpr double mebibyte = 1024.0 * 1024.0;
        return std::format(
            "the first leg again came to {:.1f} MiB of live device memory against {:.1f}, {} vertices "
            "in use against {}, and a {:.2f} ms median frame against {:.2f}",
            static_cast<double>(again.mDeviceLive) / mebibyte, static_cast<double>(first.mDeviceLive) / mebibyte,
            again.mVerticesUsed, first.mVerticesUsed, again.mMedianMs, first.mMedianMs);
    }

    std::string describeFindings(const FindingTable& table)
    {
        std::vector<const Finding*> ordered;
        for (const Finding& finding : table.getFindings())
            ordered.push_back(&finding);
        // Faults first, by kind; then the costs, the worst first within a kind.
        std::ranges::stable_sort(ordered, [](const Finding* a, const Finding* b) {
            if (isFault(a->mKind) != isFault(b->mKind))
                return isFault(a->mKind);
            if (a->mKind != b->mKind)
                return a->mKind < b->mKind;
            return a->mWorstMs > b->mWorstMs;
        });

        std::size_t faults = 0;
        for (const Finding* finding : ordered)
            faults += isFault(finding->mKind) ? 1 : 0;

        std::string text
            = std::format("\ntour findings: {} faults, {} costs and marks{}\n", faults, ordered.size() - faults,
                table.getDropped() > 0 ? std::format(", {} more past the table's bound", table.getDropped()) : "");
        for (const Finding* finding : ordered)
        {
            const FindingPlace& place = finding->mFirst;
            text += std::format("\n{} {}: {}{}{}\n  first at {} frame {}, day {} {} under {}\n",
                isFault(finding->mKind) ? "FAULT" : "cost ", findingName(finding->mKind), finding->mDetail,
                finding->mCount > 1 ? std::format(", {} times", finding->mCount) : "",
                finding->mWorstMs > 0.0 ? std::format(", worst {:.1f} ms", finding->mWorstMs) : "", place.mLeg,
                place.mFrame, place.mStood.mSky.mDay.value_or(0) + 1,
                describeHour(place.mStood.mSky.mHour.value_or(0.0f)), describeSky(place));
            if (place.mStood.mStand.mEye.has_value())
                text += describeBlock(place.mStood);
        }
        return text;
    }
}
