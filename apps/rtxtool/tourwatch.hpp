#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/logtally.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/world/frameworld.hpp>

#include "standingnote.hpp"

namespace RtxTool
{
    /// What a tour meets, turned into findings with the place each was met at: the stores a shader made that were not
    /// finite, by module; what the content gave that the renderer refused, by kind; the hitches and the slow frames;
    /// the cells a leg outran; the claims of `check` a leg failed at its end; a leg that failed; the watcher's marks.
    /// The only part of a tour that runs every frame, and a frame that finds nothing takes no place and builds no
    /// string; the place is the note's (`StandingNote`), taken when something is found.
    class TourWatch
    {
    public:
        /// @param stepSeconds the run's step, which is the frame budget: a slow frame is over it.
        /// @param paced whether the frames are held to the step, where a slow frame is the limiter's.
        TourWatch(float stepSeconds, bool paced);

        /// A leg begins.
        void begin(const Stop& stop);

        /// The frame `report` describes, of the leg begun: `measured` once the leg's frames are counted, which a
        /// hitch and a slow frame are judged on alone, as the frames ahead of them wait for the world.
        void frame(const MWRender::FrameContext& context, const MWRender::FrameReport& report, StandingNote& note,
            bool measured);

        /// The leg ended, `place` what it came to and `failed` the claims it asked that did not hold: those, its slow
        /// frames, the cells it covers that no frame stood whole in, and the closing leg's growth past the first.
        void endLeg(const Stop& stop, const BenchPlace& place, std::span<const Check> failed, StandingNote& note);

        /// The leg could not go on, for `why`.
        void failLeg(const Stop& stop, std::string_view why, StandingNote& note);

        /// Where the watcher pressed Home.
        void mark(StandingNote& note);

        /// What the log is heard by, from any thread.
        LogTally& getLog() { return mLog; }

        /// Takes in the log's lines heard so far, which a leg's end does too.
        void drainLog() { mLog.drainInto(mTable); }

        const FindingTable& getTable() const { return mTable; }

    private:
        FindingPlace placeOf(StandingNote& note);

        void add(FindingKind kind, std::string_view detail, StandingNote& note, double ms = 0.0);

        double mBudgetMs = 0.0;
        bool mPaced = false;

        /// The leg's name and how many of its frames were seen.
        std::string mLeg;
        std::uint32_t mFrame = 0;

        /// The frame before's time, which a hitch stands out from.
        double mLastMs = 0.0;

        /// The cell the player stood in on the frame before, which tells a frame that crossed into another.
        const void* mLastCell = nullptr;

        /// The refusals counted so far, by kind, so a frame's new ones are the difference.
        std::array<std::uint32_t, Rtx::sRefusedKinds> mRefused{};

        /// The columns of the leg's cells a frame stood whole in while the player was there.
        std::vector<int> mCovered;

        /// The leg's slow frames, the worst of them and where it stood.
        std::uint32_t mSlow = 0;
        std::uint32_t mMeasured = 0;
        double mSlowWorstMs = 0.0;
        std::optional<FindingPlace> mSlowWorst;

        /// What the soak's first pass came to, once it is flown.
        std::optional<SoakFigures> mFirstPass;

        /// The last air the frames reported, which a place taken at a leg's end stands under.
        Rtx::AirClock mAir{};

        FindingTable mTable;
        LogTally mLog;
    };
}
