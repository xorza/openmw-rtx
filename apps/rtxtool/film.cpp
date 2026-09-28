#include "film.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <osg/Math>

#include <components/crashcatcher/crash.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/contract.hpp>
#include <components/rtx/skylight.hpp>

#include "model/benchrecord.hpp"
#include "model/benchspec.hpp"
#include "model/blockfile.hpp"
#include "model/skycrossing.hpp"

namespace RtxTool
{
    std::vector<FilmKey> readKeys(std::istream& in, std::string source)
    {
        const BlockFile file(in, std::move(source));

        std::vector<FilmKey> keys;
        keys.reserve(file.getBlocks().size());
        for (const Block& block : file.getBlocks())
        {
            FilmKey& key = keys.emplace_back(FilmKey{ .mStop = Stop{ .mName = block.mName }, .mLine = block.mLine });
            for (const BlockField& field : block.mFields)
            {
                if (file.readPlace(field, key.mStop))
                    continue;

                if (field.mName == "day")
                    key.mStop.mSky.mDay = file.day(field);
                else if (field.mName == "seconds")
                    key.mSeconds = file.positive(field, "a length of time");
                else if (field.mName == "hold")
                    key.mHold = file.notNegative(field, "a length of time");
                else if (field.mName == "cut")
                    key.mCut = file.boolean(field);
                else
                    file.refuseUnknown(field, "key");
            }

            // `pos` and `look` are the pair Home always writes, and a key without them has no
            // facing to fly through.
            const Stand& stand = key.mStop.mStand;
            if (stand.mCell.empty())
                file.refuse(block.mLine, std::format("key \"{}\" names no cell", block.mName));
            if (!stand.mEye.has_value() || !stand.mLook.has_value())
                file.refuse(block.mLine, std::format("key \"{}\" names no pos and look", block.mName));

            StopSky& sky = key.mStop.mSky;
            sky.mHour = sky.mHour.value_or(sDefaultHour);
            sky.mWeather = sky.mWeather.value_or(std::string(sDefaultWeather));
        }

        if (keys.empty())
            throw std::runtime_error(std::format("{} states no keys", file.getSource()));

        return keys;
    }

    std::vector<FilmKey> loadKeys(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("cannot read the keys in " + Files::pathToUnicodeString(path));

        return readKeys(in, Files::pathToUnicodeString(path));
    }

    bool isExteriorCell(const std::string_view cell)
    {
        const std::size_t comma = cell.find(',');
        if (comma == std::string_view::npos)
            return false;

        const auto whole = [](std::string_view text) {
            text = trimmed(text);
            int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && error == std::errc() && end == text.data() + text.size();
        };

        return whole(cell.substr(0, comma)) && whole(cell.substr(comma + 1));
    }

    std::uint32_t FilmPacing::framesOf(const float seconds) const
    {
        return std::max(1u, BenchSpan{ .mSeconds = seconds }.getFrames(mStep));
    }

    double clockHoursPerSecond(const float clock)
    {
        return double{ clock } * double{ sGameTimeScale } / 3600.0;
    }

    std::uint32_t FilmPlan::getFrames() const
    {
        return mTakes.empty() ? 0 : mTakes.back().mFirstFrame + mTakes.back().getFrames();
    }

    namespace
    {
        osg::Vec3f rotationOf(const FilmKey& key)
        {
            return key.mStop.mStand.getRotation();
        }

        /// Why `key` cuts away from `before`, or nothing where the camera flies from one to the
        /// other. The jump is written where the distance decided.
        std::optional<FilmCut> cutBetween(const FilmKey& before, const FilmKey& key, float cutDistance, float& jump)
        {
            if (key.mCut.has_value())
                return *key.mCut ? std::optional(FilmCut::Asked) : std::nullopt;

            const bool outside = isExteriorCell(key.getCell());
            if (isExteriorCell(before.getCell()) != outside)
                return outside ? FilmCut::Outdoors : FilmCut::Indoors;

            if (!outside && key.getCell() != before.getCell())
                return FilmCut::Interior;

            jump = (key.getEye() - before.getEye()).length();
            if (jump > cutDistance)
                return FilmCut::Distance;

            return std::nullopt;
        }

        /// The segment from `from` into `to`: every change it makes but the eye's, which the path
        /// measures, and the longest of the lengths those changes ask for.
        FilmSegment sketchSegment(const FilmKey& from, const FilmKey& to, std::size_t index, const FilmPacing& pacing)
        {
            FilmSegment segment{ .mTo = index };

            const osg::Vec3f turnFrom = rotationOf(from);
            const osg::Vec3f turnTo = rotationOf(to);
            const float yaw = std::abs(shortestTurn(turnFrom.z(), turnTo.z()));
            const float pitch = std::abs(turnTo.x() - turnFrom.x());

            segment.mTurnDegrees = osg::RadiansToDegrees(std::max(yaw, pitch));
            segment.mHours = hoursForward(from.getHour(), to.getHour());

            // A pan is paced against the picture it sweeps: one image width in `mPanSeconds`
            // across, one image height up or down.
            const float vertical = osg::DegreesToRadians(pacing.mFieldOfView);
            const float horizontal = 2.0f * std::atan(std::tan(vertical / 2.0f) * pacing.mAspect);

            const std::pair<float, FilmPace> asks[] = {
                { std::max(yaw / horizontal, pitch / vertical) * pacing.mPanSeconds, FilmPace::Turn },
                { pacing.mClock.has_value() ? 0.0f : segment.mHours * pacing.mHourSeconds, FilmPace::Clock },
                { pacing.mTurn.empty() && from.getWeather() != to.getWeather() ? pacing.mCrossingSeconds : 0.0f,
                    FilmPace::Weather },
            };

            for (const auto& [asked, pace] : asks)
                if (asked > segment.mAsked)
                {
                    segment.mAsked = asked;
                    segment.mAsker = pace;
                }

            return segment;
        }

        TrackKey trackKey(const FilmKey& key, bool rests)
        {
            return TrackKey{ .mEye = key.getEye(),
                .mRotation = rotationOf(key),
                .mHour = key.getHour(),
                .mWeather = *Rtx::weatherIndex(key.getWeather()),
                .mRests = rests };
        }

        /// Where a take begins and ends among the plan's keys, and why it cuts in.
        struct TakeRange
        {
            std::size_t mFirst = 0;
            std::size_t mEnd = 0;
            FilmCut mCut = FilmCut::First;
            float mJump = 0.0f;
        };

        /// A take as far as it goes before a speed is chosen: its keys laid out and the path through
        /// them, every segment the speed has no say in timed, and the flights that are left.
        struct TakeDraft
        {
            FilmTake mTake;

            /// Per key of the take, the index in the track of the key the eye leaves it from: the
            /// second of a hold's two.
            std::vector<std::size_t> mLeaves;

            /// Per key, the frames it is held for.
            std::vector<std::uint32_t> mHolds;

            /// The flights, and which segment each is.
            std::vector<CruiseLeg> mLegs;
            std::vector<std::size_t> mFlown;

            /// The frames of the take the speed has no say in: its holds, its stills, what stands on
            /// the spot, and the keys' own seconds.
            std::uint32_t mFixed = 0;

            /// The frames its flights take at `speed`, in units per frame.
            double flightFramesAt(const Cruise& cruise, double speed) const
            {
                double frames = 0.0;
                for (const CruiseLeg& leg : mLegs)
                    frames += cruise.timeFor(leg, speed);
                return frames;
            }
        };

        /// `range`'s keys at no frames yet, the path through them, and what of the take is timed
        /// whatever the speed. A hold is the key twice, resting on both, so the camera stops on it
        /// and sets off again; a take of one key holds it for a still at least.
        TakeDraft draftTake(const FilmPlan& plan, const TakeRange& range)
        {
            const FilmPacing& pacing = plan.mPacing;
            const bool alone = range.mEnd - range.mFirst == 1;

            std::vector<TrackKey> track;
            std::vector<std::size_t> leavesFrom;
            std::vector<std::uint32_t> holds;
            std::uint32_t fixed = 0;
            for (std::size_t at = range.mFirst; at < range.mEnd; ++at)
            {
                const FilmKey& key = plan.mKeys[at];
                const float hold = alone ? std::max(key.mHold, pacing.mStillSeconds) : key.mHold;
                const std::uint32_t held = hold > 0.0f ? pacing.framesOf(hold) : 0u;

                track.push_back(trackKey(key, held > 0));
                if (held > 0)
                    track.push_back(trackKey(key, true));
                leavesFrom.push_back(track.size() - 1);
                holds.push_back(held);
                fixed += held;
            }

            CameraPath path(track);

            std::vector<FilmSegment> segments;
            std::vector<CruiseLeg> legs;
            std::vector<std::size_t> flown;
            for (std::size_t at = range.mFirst + 1; at < range.mEnd; ++at)
            {
                const FilmKey& to = plan.mKeys[at];
                FilmSegment& segment = segments.emplace_back(sketchSegment(plan.mKeys[at - 1], to, at, pacing));

                const std::size_t leaves = leavesFrom[at - range.mFirst - 1];
                segment.mDistance = path.getLength(leaves);

                if (to.mSeconds.has_value())
                {
                    segment.mPace = FilmPace::Given;
                    segment.mFrames = pacing.framesOf(*to.mSeconds);
                }
                else if (segment.mDistance > 0.0)
                {
                    segment.mPace = FilmPace::Distance;
                    legs.push_back(CruiseLeg{ .mLength = segment.mDistance,
                        .mFromRest = path.restsAt(leaves),
                        .mToRest = path.restsAt(leaves + 1) });
                    flown.push_back(segments.size() - 1);
                    continue;
                }
                else
                {
                    segment.mPace = segment.mAsked > 0.0f ? segment.mAsker : FilmPace::Still;
                    segment.mFrames = pacing.framesOf(segment.mAsked > 0.0f ? segment.mAsked : pacing.mStillSeconds);
                }

                fixed += static_cast<std::uint32_t>(segment.mFrames);
            }

            return TakeDraft{ .mTake = FilmTake{ .mFirst = range.mFirst,
                                  .mEnd = range.mEnd,
                                  .mCut = range.mCut,
                                  .mJump = range.mJump,
                                  .mSegments = std::move(segments),
                                  .mTrack = std::move(track),
                                  .mPath = std::move(path) },
                .mLeaves = std::move(leavesFrom),
                .mHolds = std::move(holds),
                .mLegs = std::move(legs),
                .mFlown = std::move(flown),
                .mFixed = fixed };
        }

        /// `total` frames shared out as `shares` ask, each a whole number and the sum exact: each
        /// share's whole part, then one more to each of the largest remainders, which between them
        /// are exactly what is left over. A share of anything at all is a frame at least, since a
        /// flight in no frames is no flight, taken from the largest; `total` covers one for each.
        std::vector<std::uint32_t> apportion(const std::vector<double>& shares, const std::uint32_t total)
        {
            const auto owed = static_cast<std::uint32_t>(
                std::count_if(shares.begin(), shares.end(), [](double share) { return share > 0.0; }));
            Rtx::contract(total >= owed, "fewer frames than flights to share them out to");

            std::vector<std::uint32_t> whole(shares.size());
            std::uint32_t given = 0;
            for (std::size_t at = 0; at < shares.size(); ++at)
            {
                whole[at] = static_cast<std::uint32_t>(std::floor(shares[at]));
                given += whole[at];
            }
            Rtx::contract(given <= total, "shares that add to more than their total");

            std::vector<std::size_t> order;
            for (std::size_t at = 0; at < shares.size(); ++at)
                if (shares[at] > 0.0)
                    order.push_back(at);
            std::stable_sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) {
                return shares[a] - std::floor(shares[a]) > shares[b] - std::floor(shares[b]);
            });
            for (std::size_t at = 0; given < total; ++at, ++given)
            {
                Rtx::contract(at < order.size(), "more left over than the shares have remainders");
                ++whole[order[at]];
            }

            for (std::size_t at = 0; at < shares.size(); ++at)
                if (shares[at] > 0.0 && whole[at] == 0)
                {
                    --*std::max_element(whole.begin(), whole.end());
                    whole[at] = 1;
                }

            return whole;
        }

        /// Times `draft` with its flights sharing `flightFrames` at one speed, into its take: each
        /// key's frame, each segment's frames and arrival, and the speed. The take then ends on the
        /// whole frame its fixed frames and its flights' add to.
        FilmTake finishTake(TakeDraft draft, const std::uint32_t flightFrames, const FilmPacing& pacing)
        {
            FilmTake& take = draft.mTake;
            const Cruise cruise = pacing.getCruise();

            double speed = 0.0;
            if (!draft.mLegs.empty())
            {
                speed = cruise.speedFor(draft.mLegs, static_cast<double>(flightFrames));
                take.mSpeed = speed / double{ pacing.mStep };
                for (std::size_t at = 0; at < draft.mLegs.size(); ++at)
                    take.mSegments[draft.mFlown[at]].mFrames = cruise.timeFor(draft.mLegs[at], speed);
            }

            double frame = 0.0;
            for (std::size_t at = 0; at < draft.mLeaves.size(); ++at)
            {
                if (at > 0)
                {
                    FilmSegment& segment = take.mSegments[at - 1];
                    frame += segment.mFrames;
                    segment.mArrival = frame;
                }

                const std::size_t leaves = draft.mLeaves[at];
                const std::size_t arrives = draft.mHolds[at] > 0 ? leaves - 1 : leaves;
                take.mTrack[arrives].mFrame = frame;
                frame += static_cast<double>(draft.mHolds[at]);
                take.mTrack[leaves].mFrame = frame;
            }

            // The flights were timed to fill their frames exactly, so what is left is rounding.
            const auto last = static_cast<double>(draft.mFixed + flightFrames);
            Rtx::contract(std::abs(frame - last) < 1e-6 * std::max(1.0, last), "a take timed off its own frames");
            take.mTrack.back().mFrame = last;
            if (!take.mSegments.empty() && draft.mHolds.back() == 0)
                take.mSegments.back().mArrival = last;

            return std::move(take);
        }

        /// The sky `pacing` runs on its own, for the take that begins at the film's `first` frame.
        SkyRun skyRunOf(const FilmPacing& pacing, const std::uint32_t first)
        {
            std::optional<double> hours;
            if (pacing.mClock.has_value())
                hours = clockHoursPerSecond(*pacing.mClock) * double{ pacing.mStep };

            return SkyRun{ .mHoursPerFrame = hours,
                .mWeathers = pacing.mTurn,
                .mHoldFrames = BenchSpan{ .mSeconds = pacing.mWeatherHold }.getFrames(pacing.mStep),
                .mCrossingFrames = pacing.framesOf(pacing.mCrossingSeconds),
                .mFirstFrame = first };
        }

        /// The hour the film stands at at its `frame`th, where `key` names what the keys say and the
        /// pacing's own clock writes over it.
        float hourAt(const FilmPlan& plan, const std::uint32_t frame, const FilmKey& key)
        {
            const SkyRun run = skyRunOf(plan.mPacing, 0);
            if (!run.mHoursPerFrame.has_value())
                return key.getHour();

            return static_cast<float>(std::fmod(
                double{ plan.mKeys.front().getHour() } + static_cast<double>(frame) * *run.mHoursPerFrame, 24.0));
        }

        /// The weather the film stands under at its `frame`th, or the two it crosses between, where
        /// `key` names what the keys say and the pacing's own turn writes over it.
        std::string weatherAt(const FilmPlan& plan, const std::uint32_t frame, const FilmKey& key)
        {
            const SkyRun run = skyRunOf(plan.mPacing, 0);
            if (run.mWeathers.empty())
                return key.getWeather();

            TrackPose sky;
            run.turnAt(frame, sky);
            if (sky.mNextWeather == sky.mWeather)
                return std::string(Rtx::weatherName(sky.mWeather));

            return std::format("{} → {}", Rtx::weatherName(sky.mWeather), Rtx::weatherName(sky.mNextWeather));
        }

        std::string_view describeCut(const FilmCut cut)
        {
            switch (cut)
            {
                case FilmCut::First:
                    return "the first key";
                case FilmCut::Asked:
                    return "the key asks for a cut";
                case FilmCut::Indoors:
                    return "outside to inside";
                case FilmCut::Outdoors:
                    return "inside to outside";
                case FilmCut::Interior:
                    return "another interior";
                case FilmCut::Distance:
                    return "too far to fly";
            }
            Crash::fatal("a cut with no name");
        }

        /// What `pace` stands for over `segment`, in the words and numbers that say it.
        std::string describeChange(
            const FilmPace pace, const FilmSegment& segment, const FilmKey& to, const FilmTake& take)
        {
            switch (pace)
            {
                case FilmPace::Given:
                    return "as the key says";
                case FilmPace::Distance:
                    return std::format("{:.0f} units at {:.0f} a second", segment.mDistance, take.mSpeed);
                case FilmPace::Turn:
                    return std::format("a turn of {:.0f}°", segment.mTurnDegrees);
                case FilmPace::Clock:
                    return std::format("{:.2f} hours of clock", segment.mHours);
                case FilmPace::Weather:
                    return std::format("the sky crossing into {}", to.getWeather());
                case FilmPace::Still:
                    return "nothing changes";
            }
            Crash::fatal("a pace with no name");
        }

        /// What set a segment's length, and on a flight what else asks for longer than the flight
        /// gives it: a turn, the clock or a crossing that a speed held from key to key hurries.
        std::string describePace(
            const FilmSegment& segment, const FilmKey& to, const FilmTake& take, const FilmPacing& pacing)
        {
            std::string text = describeChange(segment.mPace, segment, to, take);
            const double given = segment.mFrames * double{ pacing.mStep };
            if (segment.mDistance > 0.0 && double{ segment.mAsked } > given)
                text += std::format(
                    "; {} asks {:.1f} s", describeChange(segment.mAsker, segment, to, take), segment.mAsked);
            return text;
        }
    }

    FilmPlan planFilm(std::vector<FilmKey> keys, const FilmPacing& pacing)
    {
        if (keys.empty())
            throw std::runtime_error("a film needs a key");

        FilmPlan plan{ .mKeys = std::move(keys), .mPacing = pacing };

        std::vector<TakeRange> ranges;
        for (std::size_t at = 0; at < plan.mKeys.size(); ++at)
        {
            float jump = 0.0f;
            const std::optional<FilmCut> cut
                = at == 0 ? FilmCut::First : cutBetween(plan.mKeys[at - 1], plan.mKeys[at], pacing.mCutDistance, jump);
            if (cut.has_value())
                ranges.push_back(TakeRange{ .mFirst = at, .mCut = *cut, .mJump = jump });
            ranges.back().mEnd = at + 1;
        }

        std::vector<TakeDraft> drafts;
        drafts.reserve(ranges.size());
        for (const TakeRange& range : ranges)
            drafts.push_back(draftTake(plan, range));

        // **One speed for every flight of the film**, `mSpeed` or the one that fills `mLength`, in
        // world units a frame; then each take's flights rounded to the whole frames it ends on,
        // and flown at what fills those exactly — a speed off the film's by at most half a frame
        // of its own flights, where carrying the part of a frame over into the next take would
        // start it between two frames.
        const Cruise cruise = pacing.getCruise();
        std::vector<std::uint32_t> frames;
        frames.reserve(drafts.size());
        if (pacing.mLength.has_value())
        {
            std::vector<CruiseLeg> legs;
            std::uint64_t fixed = 0;
            std::uint32_t flying = 0;
            for (const TakeDraft& draft : drafts)
            {
                legs.insert(legs.end(), draft.mLegs.begin(), draft.mLegs.end());
                // And the take's first frame, which it has before anything has moved.
                fixed += std::uint64_t{ draft.mFixed } + 1;
                flying += draft.mLegs.empty() ? 0u : 1u;
            }

            const std::uint32_t length = pacing.framesOf(*pacing.mLength);
            if (legs.empty())
                throw std::runtime_error(std::format(
                    "--length has nothing to set: no key of the film is flown to, where {} s are", *pacing.mLength));
            if (length < fixed + flying)
                throw std::runtime_error(
                    std::format("--length is {} s, and the holds, the stills, what stands on the "
                                "spot and each take's first frame take {:.1f} s of it, leaving "
                                "less than a frame for each of the {} takes that fly",
                        *pacing.mLength, static_cast<double>(fixed) * double{ pacing.mStep }, flying));

            const auto flights = static_cast<std::uint32_t>(length - fixed);
            const double speed = cruise.speedFor(legs, static_cast<double>(flights));
            std::vector<double> shares;
            shares.reserve(drafts.size());
            for (const TakeDraft& draft : drafts)
                shares.push_back(draft.mLegs.empty() ? 0.0 : draft.flightFramesAt(cruise, speed));
            frames = apportion(shares, flights);
        }
        else
        {
            const double speed = double{ pacing.mSpeed } * double{ pacing.mStep };
            for (const TakeDraft& draft : drafts)
                frames.push_back(draft.mLegs.empty()
                        ? 0u
                        : std::max(1u, static_cast<std::uint32_t>(std::lround(draft.flightFramesAt(cruise, speed)))));
        }

        std::uint32_t first = 0;
        for (std::size_t at = 0; at < drafts.size(); ++at)
        {
            FilmTake& take = plan.mTakes.emplace_back(finishTake(std::move(drafts[at]), frames[at], pacing));
            take.mFirstFrame = first;
            take.mSky = skyRunOf(pacing, first);
            first += take.getFrames();
        }

        return plan;
    }

    std::string describePlan(const FilmPlan& plan)
    {
        const auto seconds = [&](double frames) { return frames * double{ plan.mPacing.mStep }; };

        // The film's frame a key is drawn nearest to, which its hour and weather are read at.
        const auto drawnAt = [](const std::uint32_t first, const double frame) {
            return first + static_cast<std::uint32_t>(std::lround(frame));
        };

        // `:g`, because the rate is one over the step and reads back as 59.999996 otherwise.
        std::string text = std::format("film: {} keys, {} takes, {} frames, {:.1f} s at {:g} frames a second\n",
            plan.mKeys.size(), plan.mTakes.size(), plan.getFrames(), seconds(plan.getFrames()), plan.mPacing.getRate());

        const FilmPacing& pacing = plan.mPacing;
        if (pacing.mClock.has_value())
            text += std::format("the clock at ×{:g} of the game's own over the whole film, {:.2f} hours a second\n",
                *pacing.mClock, clockHoursPerSecond(*pacing.mClock));
        if (!pacing.mTurn.empty())
        {
            std::string names;
            for (const std::uint32_t weather : pacing.mTurn)
                names += std::format("{}{}", names.empty() ? "" : ", ", Rtx::weatherName(weather));
            text += std::format(
                "the weather through {} and round again, each standing {:.1f} s and crossing in {:.1f} s\n", names,
                pacing.mWeatherHold, pacing.mCrossingSeconds);
        }

        for (std::size_t number = 0; number < plan.mTakes.size(); ++number)
        {
            const FilmTake& take = plan.mTakes[number];
            const FilmKey& first = plan.mKeys[take.mFirst];

            text += std::format("\ntake {}, from frame {}: {:.1f} s, cut in: {}", number + 1, take.mFirstFrame,
                seconds(take.getFrames()), describeCut(take.mCut));
            if (take.mCut == FilmCut::Distance)
                text += std::format(", {:.0f} units", take.mJump);
            text += '\n';

            const std::uint32_t start = drawnAt(take.mFirstFrame, 0.0);
            text += std::format("  {:<28} {} {}, {}{}\n", first.mStop.mName, first.getCell(),
                describeHour(hourAt(plan, start, first)), weatherAt(plan, start, first),
                first.mHold > 0.0f ? std::format(", holds {:.1f} s", first.mHold) : std::string());

            for (const FilmSegment& segment : take.mSegments)
            {
                const FilmKey& key = plan.mKeys[segment.mTo];
                const std::uint32_t arrival = drawnAt(take.mFirstFrame, segment.mArrival);

                text += std::format("  -> {:<25} {:6.1f} s  {} {}, {}{}  ({})\n", key.mStop.mName,
                    seconds(segment.mFrames), describeHour(hourAt(plan, arrival, key)), weatherAt(plan, arrival, key),
                    key.mHold > 0.0f ? std::format("holds {:.1f} s, ", key.mHold) : std::string(), key.getCell(),
                    describePace(segment, key, take, pacing));
            }
        }

        return text;
    }

    std::vector<Stop> stopsFor(const FilmPlan& plan, const std::filesystem::path& frames)
    {

        std::vector<Stop> stops;
        stops.reserve(plan.mTakes.size());
        for (std::size_t number = 0; number < plan.mTakes.size(); ++number)
        {
            const FilmTake& take = plan.mTakes[number];
            const FilmKey& first = plan.mKeys[take.mFirst];

            Stop& stop = stops.emplace_back();
            stop = first.mStop;
            stop.mName = std::format("take-{}-{}", number + 1, first.mStop.mName);
            stop.mSky.mDay = first.mStop.mSky.mDay.value_or(plan.mPacing.mDay);
            stop.mSchedule.mSpec.mWarm = BenchSpan{ .mSeconds = plan.mPacing.mWarmupSeconds };
            stop.mSchedule.mSpec.mRun = BenchSpan{ .mFrames = take.getFrames() };
            stop.mSchedule.mTrack.emplace(take.mTrack, take.mPath, plan.mPacing.getCruise(), take.mSky);
            stop.mActions.mFilm = Actions::Film{ .mDirectory = frames, .mFirst = take.mFirstFrame };

            // **A clock that runs through the film is set once, at its first take**, and every take
            // after takes up where the world stood at the last one's end: set again from a key, it
            // jumped back at every cut. A day set by hand would also leave the month where it was.
            if (number > 0 && plan.mPacing.mClock.has_value())
            {
                stop.mSky.mHour.reset();
                stop.mSky.mDay.reset();
            }

            // The turn holds the sky from the first frame, and a weather the stager settled under it
            // would be one nothing shows.
            if (!plan.mPacing.mTurn.empty())
                stop.mSky.mWeather.reset();
        }

        return stops;
    }

    namespace
    {
        constexpr std::size_t sFrameDigits = 6;
        constexpr std::string_view sFrameExtension = ".png";
    }

    std::string frameName(const std::uint32_t number)
    {
        return std::format("{:0{}}{}", number, sFrameDigits, sFrameExtension);
    }

    std::size_t clearFrames(const std::filesystem::path& frames)
    {
        if (!std::filesystem::is_directory(frames))
            return 0;

        const auto written = [](const std::string& name) {
            return name.size() == sFrameDigits + sFrameExtension.size() && name.ends_with(sFrameExtension)
                && std::all_of(name.begin(), name.begin() + sFrameDigits, [](char c) { return c >= '0' && c <= '9'; });
        };

        std::vector<std::filesystem::path> doomed;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(frames))
            if (entry.is_regular_file() && written(entry.path().filename().string()))
                doomed.push_back(entry.path());

        for (const std::filesystem::path& path : doomed)
            std::filesystem::remove(path);

        return doomed.size();
    }

    std::string encodeCommand(
        const std::filesystem::path& frames, const std::filesystem::path& video, const float framesPerSecond)
    {
        const auto word = [](const std::filesystem::path& path) {
            return Platform::Process::shellWord(Files::pathToUnicodeString(path));
        };
        return std::format(
            "ffmpeg -hide_banner -loglevel warning -y -framerate {:g} -i {} -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2\" "
            "-c:v {} -preset slow -crf {} -pix_fmt {} -movflags +faststart {}",
            framesPerSecond, word(frames / std::format("%0{}d{}", sFrameDigits, sFrameExtension)), sVideoCodec,
            sVideoQuality, sVideoPixels, word(video));
    }
}
