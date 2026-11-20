#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/measurewindow.hpp>
#include <apps/rtxtool/model/tourwatch.hpp>
#include <components/rtx/world/frameworld.hpp>

namespace MWRender
{
    struct FrameContext;
    struct FrameReport;
}

namespace RtxTool
{
    class StandingNote;

    /// **What a tour's watch is told of the game, read here and nowhere in the watch**: the frame's facts off the
    /// renderer's report and the world (`WatchFrame`), and where a finding stands (`FindingPlaces`).
    class WorldWatch final : public FindingPlaces
    {
    public:
        /// @param note the run's note, taken where a finding takes a place.
        explicit WorldWatch(StandingNote& note);

        /// The frame `report` describes as the watch reads it, `measured` the span its arrival closed where it was
        /// measured. Valid until the next ask: the active cells are kept here.
        WatchFrame frameOf(const MWRender::FrameContext& context, const MWRender::FrameReport& report,
            const std::optional<ClosedSpan>& measured);

        /// The stop that would put a camera back where the frame stands, and the sky it stands under.
        FindingPlace read(std::string_view leg, std::uint32_t frame, const Rtx::AirClock& air) override;

    private:
        StandingNote& mNote;

        /// The game's active cells on the frame asked last, refilled into room for two grids.
        std::vector<const void*> mActive;
    };
}
