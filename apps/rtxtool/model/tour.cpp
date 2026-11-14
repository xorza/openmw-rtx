#include "tour.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <osg/Math>
#include <osg/Vec2i>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <components/crashcatcher/crash.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/world/weather.hpp>

#include "benchrecord.hpp"
#include "benchspec.hpp"
#include "cruise.hpp"

namespace RtxTool
{
    namespace
    {
        constexpr double sCellSize = Constants::CellSizeInUnits;

        /// The facing of an eye flying along `direction`, looking `pitch` below the level: `(pitch, 0, yaw)`, yaw
        /// clockwise from north as `Stand::getRotation` gives it.
        osg::Vec3f facingAlong(const osg::Vec3d& direction, const float pitch)
        {
            return osg::Vec3f(pitch, 0.0f, static_cast<float>(std::atan2(direction.x(), direction.y())));
        }

        /// The land by cell, with the extent it spans.
        struct LandGrid
        {
            std::map<std::pair<int, int>, float> mHighest;
            osg::Vec2i mLow;
            osg::Vec2i mHigh;

            explicit LandGrid(const std::vector<LandCell>& land)
            {
                for (const LandCell& cell : land)
                {
                    const auto [placed, added]
                        = mHighest.emplace(std::pair(cell.mCell.x(), cell.mCell.y()), cell.mHighest);
                    if (!added)
                        placed->second = std::max(placed->second, cell.mHighest);
                }
                if (land.empty())
                    return;
                mLow = mHigh = land.front().mCell;
                for (const LandCell& cell : land)
                {
                    mLow = osg::Vec2i(std::min(mLow.x(), cell.mCell.x()), std::min(mLow.y(), cell.mCell.y()));
                    mHigh = osg::Vec2i(std::max(mHigh.x(), cell.mCell.x()), std::max(mHigh.y(), cell.mCell.y()));
                }
            }

            /// The highest ground of the cells from `x0` to `x1` and `y0` to `y1`, the sea's level where none is
            /// land: the eye flies over water too.
            float highestIn(const int x0, const int x1, const int y0, const int y1) const
            {
                float highest = 0.0f;
                for (int x = x0; x <= x1; ++x)
                    for (auto at = mHighest.lower_bound(std::pair(x, y0));
                        at != mHighest.end() && at->first.first == x && at->first.second <= y1; ++at)
                        highest = std::max(highest, at->second);
                return highest;
            }
        };

        /// A row of the sweep: its centre and the runs of land along it, each a span of columns.
        struct RowRun
        {
            int mFirst = 0;
            int mLast = 0;
        };

        /// The runs of land in the row centred at `centre`, `radius` cells either side, split where more than
        /// `spacing` columns of sea lie between two.
        std::vector<RowRun> runsOf(const LandGrid& grid, const int centre, const int radius, const int spacing)
        {
            std::set<int> columns;
            for (const auto& [cell, highest] : grid.mHighest)
                if (std::abs(cell.second - centre) <= radius)
                    columns.insert(cell.first);

            std::vector<RowRun> runs;
            for (const int column : columns)
            {
                if (!runs.empty() && column - runs.back().mLast - 1 <= spacing)
                    runs.back().mLast = column;
                else
                    runs.push_back(RowRun{ .mFirst = column, .mLast = column });
            }
            return runs;
        }

        /// Times `keys` along `path` at one speed, `speed` world units a frame or the nearest slower that ends the
        /// leg on a whole frame, eased from a rest at its ends: each key's frame and the speed it is passed at.
        void timeFlight(std::vector<TrackKey>& keys, const CameraPath& path, const Cruise& cruise, const double speed)
        {
            std::vector<CruiseLeg> legs;
            legs.reserve(path.getSegments());
            for (std::size_t segment = 0; segment < path.getSegments(); ++segment)
                legs.push_back(CruiseLeg{ .mLength = path.getLength(segment),
                    .mFromRest = path.restsAt(segment),
                    .mToRest = path.restsAt(segment + 1) });

            double time = 0.0;
            for (const CruiseLeg& leg : legs)
                time += cruise.timeFor(leg, speed);
            const double whole = std::max(1.0, std::ceil(time));
            const double flown = cruise.speedFor(legs, whole);

            double frame = 0.0;
            keys.front().mFrame = 0.0;
            keys.front().mSpeed = 0.0;
            for (std::size_t segment = 0; segment < legs.size(); ++segment)
            {
                frame += cruise.timeFor(legs[segment], flown);
                keys[segment + 1].mFrame = segment + 1 == legs.size() ? whole : frame;
                keys[segment + 1].mSpeed = flown;
            }
        }

        /// The keys of a land leg along the row centred at `centre`, from column `from` to `to` either way, one at
        /// each edge between two columns and at both ends, each a clearance over the highest ground beside it.
        std::vector<TrackKey> landKeys(
            const LandGrid& grid, const TourPacing& pacing, const int centre, const int from, const int to)
        {
            const int step = to >= from ? 1 : -1;
            const osg::Vec3d direction(step, 0.0, 0.0);
            const int radius = pacing.mGridRadius;

            std::vector<TrackKey> keys;
            // The edges a leg crosses: the near edge of its first column to the far edge of its last.
            const int firstEdge = step > 0 ? from : from + 1;
            const int lastEdge = step > 0 ? to + 1 : to;
            for (int edge = firstEdge;; edge += step)
            {
                // The ground of the two columns either side of the edge and one past each, so the spline between two
                // keys does not dip into a rise it was not told of.
                const float ground = grid.highestIn(edge - 2, edge + 1, centre - radius, centre + radius);
                keys.push_back(
                    TrackKey{ .mEye = osg::Vec3f(static_cast<float>(edge * sCellSize),
                                  static_cast<float>((centre + 0.5) * sCellSize), ground + pacing.mClearance),
                        .mRotation = facingAlong(direction, pacing.mPitch) });
                if (edge == lastEdge)
                    break;
            }
            keys.front().mRests = true;
            keys.back().mRests = true;
            return keys;
        }

        /// The keys of a room's turn once round, standing at `eye`: a quarter turn each, which the facing's spline
        /// turns the short way between two.
        std::vector<TrackKey> roomKeys(const TourPacing& pacing, const osg::Vec3f& eye)
        {
            constexpr std::size_t quarters = 4;
            const double each = static_cast<double>(pacing.framesOf(pacing.mRoomSeconds / quarters));
            std::vector<TrackKey> keys;
            for (std::size_t quarter = 0; quarter <= quarters; ++quarter)
                keys.push_back(TrackKey{ .mFrame = each * static_cast<double>(quarter),
                    .mEye = eye,
                    .mRotation = osg::Vec3f(0.0f, 0.0f, static_cast<float>(osg::PI_2 * static_cast<double>(quarter))),
                    .mRests = quarter == 0 || quarter == quarters });
            return keys;
        }

        /// The prefix before a room's comma, "Vivec" of "Vivec, Arena Pit", or the whole name where it has none.
        std::string_view settlementOf(const std::string_view name)
        {
            const std::size_t comma = name.find(',');
            return comma == std::string_view::npos ? name : name.substr(0, comma);
        }

        /// The rooms of `file` visited, by references and at most `pacing.mRoomsASettlement` a settlement: `visits`
        /// of them, fewer only where the settlements' bound leaves too few, as indices into the map's rooms.
        std::vector<std::uint32_t> roomsOf(
            const WorldMap& map, const std::uint32_t file, const std::uint32_t visits, const TourPacing& pacing)
        {
            std::vector<std::uint32_t> rooms;
            for (std::uint32_t at = 0; at < map.mRooms.size(); ++at)
                if (map.mRooms[at].mFile == file)
                    rooms.push_back(at);
            // By references, then by name, so the order is the map's and not the store's.
            std::ranges::sort(rooms, [&](const std::uint32_t a, const std::uint32_t b) {
                const Room& left = map.mRooms[a];
                const Room& right = map.mRooms[b];
                if (left.mReferences != right.mReferences)
                    return left.mReferences > right.mReferences;
                return left.mName < right.mName;
            });

            std::vector<std::uint32_t> chosen;
            std::map<std::string_view, std::uint32_t> settlements;
            for (const std::uint32_t at : rooms)
            {
                if (chosen.size() == visits)
                    break;
                std::uint32_t& kept = settlements[settlementOf(map.mRooms[at].mName)];
                if (kept == pacing.mRoomsASettlement)
                    continue;
                ++kept;
                chosen.push_back(at);
            }
            return chosen;
        }

        /// A leg's stop name: its number and where it begins.
        std::string legName(const TourLeg& leg, const std::size_t number)
        {
            const std::string_view kind
                = leg.mKind == LegKind::Land ? "land" : (leg.mKind == LegKind::Room ? "room" : "closing");
            return std::format("leg-{}-{}-{}", number + 1, kind, leg.mCell.getSpelling());
        }
    }

    std::uint32_t TourPacing::framesOf(const float seconds) const
    {
        return BenchSpan{ .mSeconds = seconds }.getFrames(mStep);
    }

    double TourPacing::getHoursPerFrame() const
    {
        return double{ mClock } * double{ mStep } * double{ mTimeScale } / 3600.0;
    }

    SkyRun TourPlan::skyRunFrom(const std::uint32_t first) const
    {
        // No clock of the track's own: the world runs it (`StopSky::mClockTimes`), and a clock the host set every
        // frame would be told to the renderer as a jump.
        return SkyRun{ .mWeathers = mCircuit,
            .mHoldFrames = BenchSpan{ .mSeconds = mPacing.mWeatherHold }.getFrames(mPacing.mStep),
            .mCrossingFrames = mPacing.framesOf(mPacing.mCrossingSeconds),
            .mFirstFrame = first };
    }

    std::uint64_t TourPlan::getFrames() const
    {
        std::uint64_t frames = 0;
        for (const TourLeg& leg : mLegs)
            frames += leg.getFrames();
        return frames;
    }

    std::vector<Rtx::Weather> weatherCircuit()
    {
        constexpr auto count = static_cast<std::size_t>(Rtx::Weather::Count);

        // Hierholzer's: walk unused edges, lowest target first, until stuck; a vertex left stuck goes onto the
        // circuit and the walk backs up to the last with an edge left.
        std::array<std::array<bool, count>, count> used{};
        std::vector<std::size_t> stack{ 0 };
        std::vector<std::size_t> circuit;
        while (!stack.empty())
        {
            const std::size_t at = stack.back();
            std::size_t next = count;
            for (std::size_t to = 0; to < count; ++to)
                if (to != at && !used[at][to])
                {
                    next = to;
                    break;
                }
            if (next == count)
            {
                circuit.push_back(at);
                stack.pop_back();
                continue;
            }
            used[at][next] = true;
            stack.push_back(next);
        }

        // The circuit comes out backwards and closed: reversed, and its repeated first vertex dropped, since the
        // turn goes round again from the last to the first.
        std::ranges::reverse(circuit);
        circuit.pop_back();
        assert(circuit.size() == count * (count - 1) && "a circuit of every ordered pair");

        std::vector<Rtx::Weather> weathers;
        weathers.reserve(circuit.size());
        for (const std::size_t weather : circuit)
            weathers.push_back(static_cast<Rtx::Weather>(weather));
        return weathers;
    }

    std::vector<std::uint32_t> shareRooms(
        const std::vector<std::uint32_t>& rooms, const std::uint32_t total, const std::uint32_t least)
    {
        std::uint64_t all = 0;
        for (const std::uint32_t count : rooms)
            all += count;

        std::vector<std::uint32_t> shares;
        shares.reserve(rooms.size());
        for (const std::uint32_t count : rooms)
        {
            const auto share = all == 0
                ? std::uint32_t{ 0 }
                : static_cast<std::uint32_t>(
                      std::lround(static_cast<double>(total) * static_cast<double>(count) / static_cast<double>(all)));
            shares.push_back(std::min(std::max(share, least), count));
        }
        return shares;
    }

    TourPlan planTour(const WorldMap& map, const TourPacing& pacing)
    {
        Crash::contract(pacing.mGridRadius >= 0, "an active grid of no radius");
        Crash::contract(pacing.mSpeed > 0.0f, "a tour that flies at no speed");

        TourPlan plan{ .mPacing = pacing, .mCircuit = weatherCircuit(), .mFiles = map.mFiles };
        for (const Room& room : map.mRooms)
            plan.mRoomReferences.push_back(room.mReferences);

        const LandGrid grid(map.mLand);
        plan.mLandCells = grid.mHighest.size();
        plan.mRoomsNoDoorEnters = map.mRoomsNoDoorEnters;
        const Cruise cruise{ .mEase = double{ pacing.mEase } / double{ pacing.mStep } };
        const double speed = double{ pacing.mSpeed } * double{ pacing.mStep };

        if (!grid.mHighest.empty())
        {
            plan.mExtent = grid.mHigh - grid.mLow + osg::Vec2i(1, 1);
            const int radius = pacing.mGridRadius;
            const int spacing = 2 * radius + 1;
            bool eastward = true;
            for (int centre = grid.mLow.y() + radius; centre - radius <= grid.mHigh.y(); centre += spacing)
            {
                std::vector<RowRun> runs = runsOf(grid, centre, radius, spacing);
                if (!eastward)
                    std::ranges::reverse(runs);
                for (const RowRun& run : runs)
                {
                    const int from = eastward ? run.mFirst : run.mLast;
                    const int to = eastward ? run.mLast : run.mFirst;
                    std::vector<TrackKey> keys = landKeys(grid, pacing, centre, from, to);

                    // **A flight goes on from the leg before where the turn between them is no longer than a row's
                    // spacing**, the turn at a row's end, which a cut would restage for nothing.
                    TourLeg leg{
                        .mKind = LegKind::Land, .mCell = CellAddress::exterior(from, centre), .mPath = CameraPath(keys)
                    };
                    if (!plan.mLegs.empty() && plan.mLegs.back().mKind == LegKind::Land)
                    {
                        const TrackKey& last = plan.mLegs.back().mTrack.back();
                        const osg::Vec3f gap = keys.front().mEye - last.mEye;
                        const double reach = static_cast<double>(spacing) * sCellSize;
                        if (std::abs(gap.x()) <= reach && std::abs(gap.y()) <= reach)
                        {
                            TrackKey turn = last;
                            turn.mRests = true;
                            keys.front().mRests = false;
                            keys.insert(keys.begin(), turn);
                            leg.mContinues = true;
                        }
                    }

                    for (int column = std::min(from, to); column <= std::max(from, to); ++column)
                        for (int row = centre - radius; row <= centre + radius; ++row)
                            if (grid.mHighest.contains(std::pair(column, row)))
                                leg.mCells.emplace_back(column, row);

                    leg.mPath = CameraPath(keys);
                    timeFlight(keys, leg.mPath, cruise, speed);
                    for (std::size_t segment = 0; segment < leg.mPath.getSegments(); ++segment)
                        leg.mLength += leg.mPath.getLength(segment);
                    leg.mTrack = std::move(keys);
                    plan.mLegs.push_back(std::move(leg));
                }
                eastward = !eastward;
            }
        }

        // The rooms: each file's share, by references, then the rooms a person named that no share took.
        std::vector<std::uint32_t> roomsAFile(map.mFiles.size(), 0);
        for (const Room& room : map.mRooms)
            ++roomsAFile.at(room.mFile);
        const std::vector<std::uint32_t> shares = shareRooms(roomsAFile, pacing.mRooms, pacing.mRoomsLeast);

        std::vector<std::uint32_t> visited;
        for (std::uint32_t file = 0; file < map.mFiles.size(); ++file)
        {
            if (roomsAFile[file] == 0)
                continue;
            const std::vector<std::uint32_t> chosen = roomsOf(map, file, shares[file], pacing);
            plan.mVisits.push_back(FileVisits{
                .mFile = file, .mRooms = roomsAFile[file], .mVisits = static_cast<std::uint32_t>(chosen.size()) });
            visited.insert(visited.end(), chosen.begin(), chosen.end());
        }
        for (const std::string& named : pacing.mNamedRooms)
            for (std::uint32_t at = 0; at < map.mRooms.size(); ++at)
                if (map.mRooms[at].mName == named && std::ranges::find(visited, at) == visited.end())
                    visited.push_back(at);

        for (const std::uint32_t at : visited)
        {
            std::vector<TrackKey> keys = roomKeys(pacing, map.mRooms[at].mEye);
            TourLeg leg{ .mKind = LegKind::Room,
                .mCell = CellAddress::interior(map.mRooms[at].mName),
                .mRoom = at,
                .mPath = CameraPath(keys) };
            leg.mTrack = std::move(keys);
            plan.mLegs.push_back(std::move(leg));
        }

        // The first land leg again, a cut under its own hours and crossings, so the two passes differ by the flight
        // between them and not by the sky.
        if (const auto first = std::ranges::find(plan.mLegs, LegKind::Land, &TourLeg::mKind); first != plan.mLegs.end())
        {
            TourLeg closing = *first;
            closing.mKind = LegKind::Closing;
            closing.mContinues = false;
            plan.mLegs.push_back(std::move(closing));
        }

        // The sky and the clock, counted in the tour's frames from its first, and begun anew at the closing leg.
        std::uint32_t frame = 0;
        const double hoursPerFrame = pacing.getHoursPerFrame();
        for (TourLeg& leg : plan.mLegs)
        {
            leg.mFirstFrame = leg.mKind == LegKind::Closing ? 0 : frame;
            const double hours = double{ pacing.mHour } + static_cast<double>(leg.mFirstFrame) * hoursPerFrame;
            const double days = std::floor(hours / 24.0);
            leg.mHour = static_cast<float>(hours - days * 24.0);
            leg.mDay = pacing.mDay + static_cast<int>(days);
            for (TrackKey& key : leg.mTrack)
                key.mHour = leg.mHour;
            if (leg.mKind != LegKind::Closing)
                frame += leg.getFrames();
        }

        return plan;
    }

    std::string describePlan(const TourPlan& plan, const float frameMs)
    {
        const TourPacing& pacing = plan.mPacing;
        std::string text;
        if (plan.mLegs.empty())
            return "the content gives a tour nothing to fly: no land and no room\n";

        std::size_t cellsFlown = 0;
        std::size_t rooms = 0;
        for (const TourLeg& leg : plan.mLegs)
        {
            if (leg.mKind == LegKind::Land)
                cellsFlown += leg.mCells.size();
            if (leg.mKind == LegKind::Room)
                ++rooms;
        }

        text += std::format("tour: {} land cells across {} x {}, rows every {} cells at {:.0f} units a second, {} up\n",
            plan.mLandCells, plan.mExtent.x(), plan.mExtent.y(), 2 * pacing.mGridRadius + 1, pacing.mSpeed,
            pacing.mClearance);
        text += std::format("  {} legs: {} cells flown, {} rooms, the first leg flown again at the end\n",
            plan.mLegs.size(), cellsFlown, rooms);
        text += std::format("  the sky through {} crossings of {:g} s, round again; the clock at {:g} times its own\n",
            plan.mCircuit.size(), pacing.mCrossingSeconds, pacing.mClock);

        const SkyRun sky = plan.skyRunFrom(0);
        for (std::size_t number = 0; number < plan.mLegs.size(); ++number)
        {
            const TourLeg& leg = plan.mLegs[number];
            TrackPose start;
            sky.turnAt(leg.mFirstFrame, start);
            const std::string skyText = start.mWeather == start.mNextWeather
                ? std::string(Rtx::nameOf(start.mWeather))
                : std::format("{} to {} {:.0f}%", Rtx::nameOf(start.mWeather), Rtx::nameOf(start.mNextWeather),
                      100.0f * start.mCrossed);
            std::string what;
            switch (leg.mKind)
            {
                case LegKind::Land:
                    what = std::format("land  {} cells, {:.0f} units", leg.mCells.size(), leg.mLength);
                    break;
                case LegKind::Room:
                    what = std::format("room  {} references", plan.mRoomReferences.at(leg.mRoom.value()));
                    break;
                case LegKind::Closing:
                    what = std::format("closing, the first land leg again, {} cells", leg.mCells.size());
                    break;
            }

            text += std::format("  {:>4} {:<9} {:>6} frames  day {} {}  {}  {}{}\n", number + 1,
                leg.mCell.getSpelling(), leg.getFrames(), leg.mDay + 1, describeHour(leg.mHour), skyText, what,
                leg.mContinues ? ", flown on" : "");
        }

        if (!plan.mVisits.empty())
        {
            text += "  rooms by file:";
            for (const FileVisits& visits : plan.mVisits)
                text += std::format(" {} {} of {};", plan.mFiles.at(visits.mFile), visits.mVisits, visits.mRooms);
            text += '\n';
        }
        if (plan.mRoomsNoDoorEnters > 0)
            text += std::format("  {} rooms no door leads into left out\n", plan.mRoomsNoDoorEnters);

        const std::uint64_t frames = plan.getFrames();
        text += std::format("  {} frames, {:.1f} min at {:g} ms a frame before the loads\n", frames,
            static_cast<double>(frames) * double{ frameMs } / 60000.0, frameMs);
        return text;
    }

    std::vector<Stop> stopsFor(const TourPlan& plan, const std::size_t from)
    {
        std::vector<Stop> stops;
        // The soak's first pass is the first land leg, where the plan has one: a tour of rooms alone has none.
        const auto firstLand = std::ranges::find(plan.mLegs, LegKind::Land, &TourLeg::mKind);
        for (std::size_t number = from; number < plan.mLegs.size(); ++number)
        {
            const TourLeg& leg = plan.mLegs[number];
            Stop& stop = stops.emplace_back();
            stop.mName = legName(leg, number);
            stop.mStand.mCell = leg.mCell;
            stop.mStand.mEye = leg.mTrack.front().mEye;
            stop.mStand.lookAt(leg.mTrack.front().mEye + Stand::forwardOf(leg.mTrack.front().mRotation));
            stop.mSky.mHour = leg.mHour;
            stop.mSky.mDay = leg.mDay;
            stop.mSky.mClockTimes = plan.mPacing.mClock;

            // A cut warms up the histories at the leg's first frame; a flight that goes on has them already. The
            // first leg flown is always a cut, wherever the tour began.
            const bool continues = leg.mContinues && number > from;
            stop.mSchedule.mSpec.mWarm = BenchSpan{ .mFrames = continues ? 0u : sHistoryFrames };
            stop.mSchedule.mSpec.mRun = BenchSpan{ .mFrames = leg.getFrames() };
            stop.mSchedule.mTrack.emplace(leg.mTrack, leg.mPath,
                Cruise{ .mEase = double{ plan.mPacing.mEase } / double{ plan.mPacing.mStep } },
                plan.skyRunFrom(leg.mFirstFrame));
            stop.mSchedule.mContinues = continues;
            stop.mActions.mCovers = leg.mCells;
            if (leg.mKind == LegKind::Closing)
                stop.mActions.mSoak = Actions::Soak::Again;
            else if (firstLand != plan.mLegs.end() && &leg == &*firstLand)
                stop.mActions.mSoak = Actions::Soak::First;
        }
        return stops;
    }
}
