#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/world/frameworld.hpp>

#include "benchrun.hpp"
#include "expectations.hpp"
#include "finding.hpp"
#include "logtally.hpp"
#include "longestframes.hpp"
#include "measurewindow.hpp"

namespace RtxTool
{
    /// **What one frame tells the watch**, the facts of the frame's report and of the world it decides by, as plain
    /// values: what reads the game is the session's (`watchFrameOf`), so a test holds the judgement with no world.
    struct WatchFrame
    {
        /// Where the air's clocks stood, which a place taken at a leg's end stands under.
        Rtx::AirClock mAir{};

        /// The kernels that stored a value that was not finite on the frame the device answered for.
        std::span<const Rtx::NotFiniteStores> mNotFinite{};

        /// The scene traced: every refusal it met so far in the order met, and how its textures came back from rest.
        const Rtx::SceneDesc& mScene;

        /// The cells the game holds active, in any order.
        std::span<const void* const> mActiveCells{};

        /// The frame drew the whole world (`FrameReport::isWhole`), and the column of the exterior cell the player
        /// stood in, nothing for an interior.
        bool mWhole = false;
        std::optional<std::int32_t> mStandingColumn{};

        /// The span the frame's arrival closed, where the frame was measured (`Measurer::getMeasuredSpan`), and when
        /// it ended on the steady clock.
        std::optional<ClosedSpan> mMeasured{};
        std::int64_t mEndNs = 0;

        /// Demanded textures drawing the stand-in, and demanded textures planned coarser than they want
        /// (`Rtx::ResidencyReport`).
        std::uint32_t mStandIns = 0;
        std::uint32_t mBelowWant = 0;

        /// Cells the ring adopted inside the drawn reach (`Rtx::ExtractionStats::mCellsAdoptedInReach`), demanded
        /// meshes standing nothing, and demanded textures the hand-over raised a level (`Rtx::ResidencyReport`).
        std::uint32_t mCellsAdoptedInReach = 0;
        std::uint32_t mMeshesMissing = 0;
        std::uint32_t mLevelSteps = 0;

        /// How long a crossing just before this frame held the game for its band, nothing where none did.
        std::optional<double> mHeldAtCrossingMs{};
    };

    /// **How long a demanded texture may stand coarser than it wants before it is a finding**: two seconds of frames at
    /// sixty, past a raise that waits a chunk's room or a want that fell and holds (`Residency::sWantHoldHandOvers`).
    inline constexpr std::uint32_t sBelowWantFramesMost = 120;

    /// Where a finding is met, read only where one takes a place: the stop that would put a camera back there and
    /// the sky it stood under, which are the world's to say.
    class FindingPlaces
    {
    public:
        virtual FindingPlace read(std::string_view leg, std::uint32_t frame, const Rtx::AirClock& air) = 0;

        FindingPlaces(const FindingPlaces&) = delete;
        FindingPlaces& operator=(const FindingPlaces&) = delete;

    protected:
        FindingPlaces() = default;
        ~FindingPlaces() = default;
    };

    /// What a tour meets, turned into findings with the place each was met at: the stores a shader made that were not
    /// finite, by module; what the content gave that the renderer refused, by kind; the hitches and the slow frames;
    /// the cells a leg outran; the claims of `check` a leg failed at its end; a leg that failed; the watcher's marks.
    /// The only part of a tour that runs every frame, and a frame that finds nothing takes no place and builds no
    /// string; the place is read when something is found (`FindingPlaces`).
    class TourWatch
    {
    public:
        /// @param stepSeconds the run's step, which is the frame budget: a slow frame is over it.
        /// @param paced whether the frames are held to the step, where a slow frame is the limiter's.
        /// @param expected what the content is known to meet, which fails no run (`--expected`).
        TourWatch(float stepSeconds, bool paced, Expectations expected);

        /// A leg begins.
        void begin(const Stop& stop);

        /// One frame of the leg begun. A hitch and a slow frame are judged on measured spans alone, as the frames
        /// ahead of them wait for the world, and a hitch is read in the span, which holds the work of the frame
        /// before with the game's update this one arrived through.
        void frame(const WatchFrame& frame, FindingPlaces& places);

        /// The leg ended, `place` what it came to and `failed` the claims it asked that did not hold: those, its slow
        /// frames, the cells it covers that no frame stood whole in, and the closing leg's growth past the first.
        void endLeg(const Stop& stop, const BenchPlace& place, std::span<const Check> failed, FindingPlaces& places);

        /// The leg could not go on, for `why`.
        void failLeg(const Stop& stop, std::string_view why, FindingPlaces& places);

        /// Where the watcher pressed Home.
        void mark(FindingPlaces& places);

        /// What the log is heard by, from any thread.
        LogTally& getLog() { return mLog; }

        /// Takes in the log's lines heard so far, which a leg's end does too.
        void drainLog() { mLog.drainInto(mTable); }

        const FindingTable& getTable() const { return mTable; }

        /// The tour's longest measured frames, as they stood on the screen, and when each ended.
        const LongestFrames& getLongest() const { return mLongest; }

    private:
        void add(FindingKind kind, std::string_view detail, FindingPlaces& places, double ms = 0.0);

        /// Counts the hitch `span` is, as it stood on the screen (`Rtx::FrameSpend::seen`), by its leg and the
        /// stretch it spent most in, placed at the worst of them with `loaded` the cells the game made active on it.
        void addHitch(const ClosedSpan& span, std::size_t loaded, FindingPlaces& places);

        double mBudgetMs = 0.0;
        bool mPaced = false;

        /// The leg's name and how many of its frames were seen.
        std::string mLeg;
        std::uint32_t mFrame = 0;

        /// The frame before's time, which a hitch stands out from.
        double mLastMs = 0.0;

        /// The game's active cells on the frame before and on this one, sorted, which tell a frame the game loaded
        /// cells on: it changes its grid apart from the frame the player's own cell changes. Refilled each frame
        /// into room reserved for two grids.
        std::vector<const void*> mActive;
        std::vector<const void*> mLastActive;

        /// How many of the scene's refusals, in the order met, are counted so far, so a frame's new ones are the rest.
        std::size_t mRefusalsRead = 0;

        /// How the textures came back from rest over the tour.

        LongestFrames mLongest;

        /// The columns of the leg's cells a frame stood whole in while the player was there.
        std::vector<std::int32_t> mCovered;

        /// The leg's measured frames that drew a stand-in, and where the first stood; the frames on end a demanded
        /// texture stood below its want, the longest run and where it ended.
        std::uint32_t mStandInFrames = 0;
        std::uint32_t mStandInsMost = 0;
        std::optional<FindingPlace> mStandInFirst;
        std::uint32_t mBelowWantRun = 0;
        std::uint32_t mBelowWantLongest = 0;
        std::optional<FindingPlace> mBelowWantAt;

        /// How often something was met in the leg, and where first.
        struct Met
        {
            FindingPlace mFirst;
            std::uint32_t mCount = 0;
        };

        /// Counts `count` more of what `met` follows, placed at this frame where it is the first.
        void note(std::optional<Met>& met, std::uint32_t count, FindingPlaces& places);

        /// Whether a frame of the leg stood whole yet, past which a cell adopted inside the reach is a pop; the pops,
        /// the measured frames a demanded mesh stood nothing on, and the textures raised in view.
        bool mStoodWhole = false;
        std::optional<Met> mPops;
        std::optional<Met> mLateStands;
        std::optional<Met> mLevelSteps;

        /// The crossings held for their band: how many, how long in all, and the longest and where it was met.
        struct Held
        {
            FindingPlace mWorst;
            double mWorstMs = 0.0;
            double mTotalMs = 0.0;
            std::uint32_t mCount = 0;
        };
        std::optional<Held> mHeld;

        /// The leg's slow frames, the worst of them and where it stood.
        std::uint32_t mSlow = 0;
        std::uint32_t mMeasured = 0;
        double mSlowWorstMs = 0.0;
        std::optional<FindingPlace> mSlowWorst;

        /// What the soak's first pass came to, once it is flown.
        std::optional<SoakFigures> mFirstPass;

        /// The last air the frames reported, which a place taken at a leg's end stands under.
        Rtx::AirClock mAir{};

        /// A hitch's detail, refilled for each hitch: a hitch that takes no place builds no string.
        std::string mHitch;

        FindingTable mTable;
        LogTally mLog;
    };
}
