#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include <components/rtx/world/weather.hpp>

#include "benchrun.hpp"
#include "camerapath.hpp"
#include "cameratrack.hpp"
#include "skycrossing.hpp"

namespace RtxTool
{
    /// One exterior cell the content gives land, and the highest its ground stands.
    struct LandCell
    {
        osg::Vec2i mCell;
        float mHighest = 0.0f;
    };

    /// One interior, as the tour reads it off the content: its name, the content file that first names it, how many
    /// references it holds, and where the eye stands in it, which is where `coc` puts a player.
    struct Room
    {
        std::string mName;
        std::uint32_t mFile = 0;
        std::uint32_t mReferences = 0;
        osg::Vec3f mEye{};
    };

    /// What the content that loaded holds, as a tour is planned from it: read once from the game's store
    /// (`readWorldMap`), so the route covers whatever a profile adds.
    struct WorldMap
    {
        std::vector<LandCell> mLand;

        /// The content files, in load order, which a room names by index.
        std::vector<std::string> mFiles;

        std::vector<Room> mRooms;

        /// The rooms left out because no door leads into them: a content's test cells, and what only a script
        /// reaches.
        std::uint32_t mRoomsNoDoorEnters = 0;
    };

    /// What paces a tour: the command line's, each a default `tour --help` states.
    struct TourPacing
    {
        /// How long one frame stands for, which every length is counted in frames by.
        float mStep = sStepSeconds;

        /// World units a second the eye flies at: the `streaming` suite's, so the ring arrives as it does under
        /// `island-crossing`.
        float mSpeed = 12000.0f;

        /// Seconds the eye takes to reach its speed from a rest and to come back to one (`Cruise`).
        float mEase = 1.0f;

        /// How far above the highest ground under it the eye flies.
        float mClearance = 2048.0f;

        /// How far the eye looks down from the level, in radians, so the ground ahead fills the frame.
        float mPitch = 0.35f;

        /// The active grid's radius in cells (`CellGridRadius`): the rows are `2r + 1` cells apart, so every land
        /// cell stands in the active grid once.
        int mGridRadius = 1;

        /// Seconds each crossing of the circuit of weathers takes, and how long each weather stands before the next.
        float mCrossingSeconds = sTurnSeconds;
        float mWeatherHold = 0.0f;

        /// The game clock's speed as a multiple of the game's own, and the world's own `timescale`, which turns it
        /// into game hours: read where the world first stands, as the plan is made there.
        float mClock = 24.0f;
        float mTimeScale = 30.0f;

        /// The hour and the day the first leg begins at.
        float mHour = 9.0f;
        int mDay = 0;

        /// How many rooms the interiors share out by the files that add them, the least a file is given, and the most
        /// a settlement keeps of a file's share, by the prefix before a room's comma.
        std::uint32_t mRooms = 60;
        std::uint32_t mRoomsLeast = 8;
        std::uint32_t mRoomsASettlement = 3;

        /// Seconds a room's turn once round takes.
        float mRoomSeconds = 4.0f;

        /// Rooms a person names as mattering, by name (`[interiors]` in `benches.cfg`), visited on top of the shares
        /// where the content holds them.
        std::vector<std::string> mNamedRooms;

        /// Seconds as a whole count of frames at the step, one at least.
        std::uint32_t framesOf(float seconds) const;

        /// Game hours one frame of the tour runs the clock by.
        double getHoursPerFrame() const;
    };

    /// What a leg flies.
    enum class LegKind : std::uint8_t
    {
        /// A run of land along one row of the sweep.
        Land,

        /// A room, turned once round where `coc` puts a player.
        Room,

        /// The first land leg flown again at the end, under the first leg's hours and crossings.
        Closing,
    };

    /// One leg of a tour: one stop, one flight.
    struct TourLeg
    {
        LegKind mKind = LegKind::Land;

        /// Where the leg's stop stands: the first cell it flies, or its room.
        CellAddress mCell;

        /// Whether the flight goes on from the leg before's last frame, the turn between two rows flown, so its stop
        /// restages nothing.
        bool mContinues = false;

        /// The cells the leg covers, each standing in the active grid while it flies: a land leg's.
        std::vector<osg::Vec2i> mCells{};

        /// The room a room's leg turns in, among the map's.
        std::optional<std::uint32_t> mRoom{};

        /// The keys at their frames, and the line through them.
        std::vector<TrackKey> mTrack{};
        CameraPath mPath;

        /// The tour's frame the leg's first frame is, which its sky is counted from.
        std::uint32_t mFirstFrame = 0;

        /// The hour and the day it begins at.
        float mHour = 0.0f;
        int mDay = 0;

        /// How far the eye flies over it, in world units.
        double mLength = 0.0;

        std::uint32_t getFrames() const { return static_cast<std::uint32_t>(mTrack.back().mFrame) + 1; }
    };

    /// The rooms one content file gives a tour: how many it adds and how many are visited.
    struct FileVisits
    {
        std::uint32_t mFile = 0;
        std::uint32_t mRooms = 0;
        std::uint32_t mVisits = 0;
    };

    /// A content's tour, planned before a frame is flown.
    struct TourPlan
    {
        TourPacing mPacing;
        std::vector<TourLeg> mLegs{};

        /// The weathers the sky turns through, round and round (`weatherCircuit`).
        std::vector<Rtx::Weather> mCircuit{};

        /// The rooms each content file that adds any gives, in load order.
        std::vector<FileVisits> mVisits{};

        /// The map's content files and each room's references, which the description prints.
        std::vector<std::string> mFiles{};
        std::vector<std::uint32_t> mRoomReferences{};

        std::size_t mLandCells = 0;
        std::uint32_t mRoomsNoDoorEnters = 0;
        osg::Vec2i mExtent{};

        /// Every leg's frames.
        std::uint64_t getFrames() const;

        /// The sky the tour runs over a leg beginning at the tour's `first` frame: `mCircuit`, at the pacing's pace.
        SkyRun skyRunFrom(std::uint32_t first) const;
    };

    /// The ten weathers in an order whose consecutive pairs, the last back to the first, are every ordered pair of
    /// two different weathers once: an Euler circuit of the complete digraph on them (Hierholzer's, from `Clear`, the
    /// lowest weather left first), so ninety crossings cover every change the sky can make.
    std::vector<Rtx::Weather> weatherCircuit();

    /// How many rooms each of `rooms` files is visited at: its share of `total` by the rooms it adds, rounded, never
    /// under `least` nor over what it has. Proportional allocation with a minimum a stratum.
    std::vector<std::uint32_t> shareRooms(
        const std::vector<std::uint32_t>& rooms, std::uint32_t total, std::uint32_t least);

    /// The route through `map`: the land by a boustrophedon sweep, rows `2r + 1` cells apart and a leg a run of
    /// land in a row, the sea of more than a row's spacing between two runs a cut; then the rooms, each file's
    /// share by references; then the first land leg again. The sky turns and the clock runs from the first frame
    /// to the last, the closing leg's begun anew. A function of the map and the pacing and nothing else, so two
    /// plans of one map agree frame for frame.
    TourPlan planTour(const WorldMap& map, const TourPacing& pacing);

    /// The plan as a person reads it before a flight: every leg, its cells, its length, its frames, the hour and
    /// the sky it begins under; the rooms each file gives; and the minutes the frames take at `frameMs`.
    std::string describePlan(const TourPlan& plan, float frameMs);

    /// One stop a leg, from the `from`th leg on: each a flight that begins where and under what the plan says, so a
    /// tour begun at a later leg stands as the whole tour stood there.
    std::vector<Stop> stopsFor(const TourPlan& plan, std::size_t from = 0);
}
