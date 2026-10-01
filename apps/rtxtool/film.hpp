#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include "model/benchrun.hpp"
#include "model/camerapath.hpp"
#include "model/cameratrack.hpp"
#include "model/cruise.hpp"

namespace RtxTool
{
    /// One key of a film, as a keys file states it: the place a window prints on Home, and what a
    /// film does at it and on the way to it.
    struct FilmKey
    {
        /// Where it stands and under what sky: a stop, whose cell, eye and look `readKeys` requires
        /// and whose hour and weather it settles at the file's own where the block names none, the
        /// rule `describeBlock` leaves them out by. The day stays unsaid where the block names none.
        Stop mStop;

        const std::string& getCell() const { return mStop.mStand.mCell; }
        const osg::Vec3f& getEye() const { return *mStop.mStand.mEye; }
        const osg::Vec3f& getLook() const { return *mStop.mStand.mLook; }
        float getHour() const { return *mStop.mSky.mHour; }
        const std::string& getWeather() const { return *mStop.mSky.mWeather; }

        /// How long the flight to this key takes, in place of the length its changes derive.
        std::optional<float> mSeconds{};

        /// How long the camera rests here.
        float mHold = 0.0f;

        /// Whether a cut comes before this key: forced, forbidden, or left to the distance.
        std::optional<bool> mCut{};

        /// The line the key's section opens on, which a message about it names.
        std::size_t mLine = 0;
    };

    /// The keys `in` states, in order. A section name may repeat, since a file is a list of keys
    /// and a window pressed twice in one place names both after it. Throws naming `source` and the
    /// line for anything malformed: a key misread is a film of somewhere else.
    std::vector<FilmKey> readKeys(std::istream& in, std::string source);

    std::vector<FilmKey> loadKeys(const std::filesystem::path& path);

    /// Whether `cell` is an exterior, spelt as a pair of integers the way `--cell` takes one.
    bool isExteriorCell(std::string_view cell);

    /// What paces a film: the command line's, each a default `film --help` states.
    struct FilmPacing
    {
        /// How long one frame of the film stands for: the run's own step (`RunSetup::mStep`), which
        /// every length below is counted in frames by, and `--fps` is one over.
        float mStep = sStepSeconds;

        /// How long a film is where the command line names neither its length nor its speed.
        static constexpr float sLengthByDefault = 20.0f;

        /// World units a second the eye flies at along the path, key to key, where `mLength` does
        /// not set it.
        float mSpeed = 800.0f;

        /// **The film's length, for the eye's speed to fill.** Every flight is flown at the one
        /// speed that ends the film at this many seconds, after its holds, its stills, what stands
        /// on the spot, and the keys' own `seconds`; or nothing for `mSpeed`.
        std::optional<float> mLength;

        /// Seconds the eye takes to reach its speed from a rest and to come back to one: at a
        /// take's ends, at a hold, and beside a turn on the spot (`Cruise`).
        float mEase = 1.0f;

        /// Seconds a pan takes to turn one image width, the established limit for judder.
        float mPanSeconds = 7.0f;

        /// Seconds of film a game hour takes, where two keys' hours differ.
        float mHourSeconds = 2.0f;

        /// The least a crossing into another weather takes, and what each crossing of `mTurn`
        /// takes.
        float mCrossingSeconds = 8.0f;

        /// **The sky on its own rate, for a time-lapse the camera flies through.** The game
        /// clock's speed over the whole film as a multiple of the game's own, `sGameTimeScale` —
        /// the `×N` the clock keys set in a window — where the hours the keys name after the first
        /// are left alone and set no segment's length; or nothing for the keys' hours.
        std::optional<float> mClock;

        /// The weathers the sky turns through over the whole film, round and round, as
        /// `Rtx::weatherIndex` numbers them, where the weathers the keys name are left alone and
        /// set no segment's length; or none for the keys' weathers.
        std::vector<std::uint32_t> mTurn;

        /// How long each weather of `mTurn` stands before the crossing into the next.
        float mWeatherHold = 4.0f;

        /// How long a key that nothing leads to or from stands, and a segment where nothing changes.
        float mStillSeconds = 4.0f;

        /// How far apart two keys of one space can be and still be flown between.
        float mCutDistance = 16384.0f;

        /// The frame's vertical field of view, in degrees, and its width over its height: what a
        /// pan's pace is read against.
        float mFieldOfView = 60.0f;
        float mAspect = 16.0f / 9.0f;

        /// Which day a take stands on where its first key names none.
        int mDay = 0;

        /// Seconds as a whole count of frames at the step, one at least: `BenchSpan`'s count,
        /// which is what the session turns a span's seconds into.
        std::uint32_t framesOf(float seconds) const;

        /// Frames a second, which is what a person reads and what the encoder is told.
        float getRate() const { return 1.0f / mStep; }

        /// The pace along the path in frames, which is what a track counts in.
        Cruise getCruise() const { return Cruise{ .mEase = double{ mEase } / double{ mStep } }; }
    };

    /// Game hours a second of a clock running at `clock` times the game's own speed,
    /// `sGameTimeScale`: `FilmPacing::mClock`'s rate.
    double clockHoursPerSecond(float clock);

    /// Which of a segment's changes set its length: the flight's speed, a key's own seconds, or
    /// for a segment that goes nowhere the longest of what else it changes.
    enum class FilmPace
    {
        Given,
        Distance,
        Turn,
        Clock,
        Weather,
        Still,
    };

    /// The flight from one key to the next, as the plan timed it.
    struct FilmSegment
    {
        /// The key it arrives at.
        std::size_t mTo = 0;

        /// How many frames it takes, and the frame of its take it arrives at: between two frames
        /// wherever a flight at one speed passes a key.
        double mFrames = 0.0;
        double mArrival = 0.0;

        FilmPace mPace = FilmPace::Still;

        /// What changes over it: how far the eye moves along the path, how far it turns, and how
        /// many hours the clock runs.
        double mDistance = 0.0;
        float mTurnDegrees = 0.0f;
        float mHours = 0.0f;

        /// The longest its turn, its clock and its weather ask, in seconds, and which of them asks
        /// it: what times a segment that goes nowhere, and what the plan holds a flight's own time
        /// against, since a flight at one speed takes what its length gives whatever else it changes.
        float mAsked = 0.0f;
        FilmPace mAsker = FilmPace::Still;
    };

    /// Why a take begins with a cut.
    enum class FilmCut
    {
        First,
        Asked,
        Indoors,
        Outdoors,
        Interior,
        Distance,
    };

    /// A run of keys with no cut between them: one flight.
    struct FilmTake
    {
        /// Its keys, as indices into the plan's, from `mFirst` up to but not including `mEnd`.
        std::size_t mFirst = 0;
        std::size_t mEnd = 0;

        FilmCut mCut = FilmCut::First;

        /// How far the cut jumped, where the distance was the reason.
        float mJump = 0.0f;

        /// The segment into each key after the first.
        std::vector<FilmSegment> mSegments;

        /// The keys at their frames, a hold stated as two, and the line through them, for
        /// `CameraTrack`.
        std::vector<TrackKey> mTrack;
        CameraPath mPath;

        /// World units a second its flights are flown at, or nought where nothing flies. The plan's
        /// speed, give or take the part of a frame the take was rounded by to end on a frame.
        double mSpeed = 0.0;

        /// The number of the take's first frame in the film.
        std::uint32_t mFirstFrame = 0;

        /// What the pacing's own sky writes over the keys', from `mFirstFrame` on.
        SkyRun mSky{};

        std::uint32_t getFrames() const { return static_cast<std::uint32_t>(mTrack.back().mFrame) + 1; }
    };

    /// A keys file split into takes and timed, before a frame is drawn.
    struct FilmPlan
    {
        std::vector<FilmKey> mKeys;
        std::vector<FilmTake> mTakes{};
        FilmPacing mPacing;

        std::uint32_t getFrames() const;
    };

    /// Splits `keys` into takes, lays each take's path through its eyes, and times it: a flight at
    /// one speed along the path, `mSpeed` or what fills `mLength`; a segment that goes nowhere by
    /// the longest of what else it changes; a key's own seconds over either. Each take ends on a
    /// whole frame, and its speed is what fills that exactly. Throws where there is no key, and
    /// where a length cannot be filled.
    FilmPlan planFilm(std::vector<FilmKey> keys, const FilmPacing& pacing);

    /// The plan as a person reads it before committing an hour of rendering to it: every take, why
    /// it cuts, every segment, how long it takes and what set that.
    std::string describePlan(const FilmPlan& plan);

    /// One stop per take, writing its frames into `frames` numbered through the whole film.
    std::vector<Stop> stopsFor(const FilmPlan& plan, const std::filesystem::path& frames);

    /// What a film's frame `number` is written as in its directory, `000042.png`: six digits, which
    /// is what ffmpeg reads the sequence back by and what `clearFrames` removes.
    std::string frameName(std::uint32_t number);

    /// Removes every file in `frames` that `frameName` could have written, and nothing else: the
    /// encoder reads a sequence up to its first gap, so a longer film's last frames left behind
    /// would play on after this one. Returns how many it removed.
    std::size_t clearFrames(const std::filesystem::path& frames);

    /// The encoder a film is written with: H.264, which every player decodes.
    inline constexpr std::string_view sVideoCodec = "libx264";

    /// Its constant rate factor, the quality it holds: eighteen is what "visually lossless" means
    /// in practice for H.264.
    inline constexpr int sVideoQuality = 18;

    /// The pixel layout every player reads, where H.264's default out of RGB frames is one most
    /// of them do not.
    inline constexpr std::string_view sVideoPixels = "yuv420p";

    /// The ffmpeg line that encodes the frames in `frames` into `video` at `framesPerSecond`, for
    /// the system's shell: `sVideoCodec` at `sVideoQuality` under the slow preset, in
    /// `sVideoPixels`; the index at the front, so a browser plays it before it has it all; and an
    /// odd side padded by one, which H.264 refuses.
    std::string encodeCommand(
        const std::filesystem::path& frames, const std::filesystem::path& video, float framesPerSecond);
}
