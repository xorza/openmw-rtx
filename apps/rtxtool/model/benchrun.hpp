#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/rtxrun.hpp>
#include <components/rtx/environment/frameworld.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>

#include "benchrecord.hpp"
#include "benchspec.hpp"
#include "cameratrack.hpp"

namespace RtxTool
{
    /// How fast a measured run steps the world unless it states otherwise, in frames a second: world
    /// time and not wall time, so ten seconds is the same six hundred frames on a build that draws
    /// them in four seconds and on one that takes twenty. Sixty because that is what the frame
    /// budget is written against. A default a run states (`RunSetup::mStep`), and never read in
    /// place of the step a run stated.
    inline constexpr float sStepRate = 60.0f;

    /// How long one of those frames stands for, which is what a measured frame tells the renderer.
    inline constexpr float sStepSeconds = 1.0f / sStepRate;

    /// What one frame of world counts for where a run turns seconds into frames — a span, a flight,
    /// a turning sky: the stated step, or where the wall decides, the step a measured run states by
    /// default.
    inline float worldStep(const MWRender::RunSetup& setup)
    {
        return setup.mStep.value_or(sStepSeconds);
    }

    /// One thing a run asserts about what the renderer was handed or what it drew, of the running
    /// game and never of a staged world, which reads its cells and dresses its people by rules of
    /// its own.
    enum class Check
    {
        /// A second walk over the same graph adds no mesh and no material, and stands no placement
        /// again: the property the incremental mirror rests on, and the only way to ask it is to
        /// ask twice.
        WalkTwice,

        /// Every placement wears a material something described. A placement wearing nothing is a
        /// surface the extractor could not read, which reaches the screen as grey.
        SurfacesDescribed,

        /// A room holds lights to cast. Asked of an interior and answered yes by every exterior,
        /// because a hillside at noon places none; a room that placed none looks dark rather than
        /// faulty.
        LightsPlaced,

        /// A route crossed cell boundaries, and not every crossing had to rebuild. An append and a
        /// rebuild are an order of magnitude apart.
        CrossingsAppend,

        /// An exterior's ground reaches past the square the simulation holds. `Rtx::CellRing`
        /// stands it off the land records, and a world stopping at the active grid looks like a
        /// short view rather than a fault.
        GroundReaches,

        /// Every cell of the reach stands its ground, one placement a cell. A cell the ring did not
        /// stand is a hole, under the player's feet as readily as at the horizon.
        GroundStands,

        /// No two lights stand at the same point. The lamps of unloaded cells are read out of the
        /// content files, and a cell that then loads brings its own copy of each.
        LightsNotDoubled,

        /// No static stands twice: nothing the cell ring stands off the content files is a
        /// reference the game has stood in its graph. The ring drops a cell's statics the frame
        /// the cell enters the active grid, and the walk finds the game's own copies there; one
        /// the ring kept would stand beside the game's, at the record's place — a building where
        /// the game put none, until the ring let go of it.
        StaticsNotDoubled,

        /// Every gate that decided agrees with the game in the cells it has loaded: a reference
        /// behind an open gate is up there and one behind a closed gate is down, as the reference's
        /// own script left it. A disagreement is a gate whose run is not the script's first frame,
        /// and a stage seen from afar that the cell changes as it loads.
        GatesAgree,

        /// Every texture the scene named could be read; an unreadable one is drawn grey.
        TexturesReadable,

        /// The frame was drawn from the camera the stop asked for, because a camera something else
        /// moved gives figures that look reasonable. Answered yes by a stop that named no camera,
        /// by a free-camera stop, and by a routed one.
        CameraStands,

        /// Two frames were in flight at every submit. The ring is sized for two and the game
        /// keeps two, so a submit that found one is a wait somebody put back — the 0.9 ms a frame
        /// the device idled for the whole of this fork's life before `Renderer::collectFrame`.
        /// Asked of a place that stands still, because an arrival drains the ring by design.
        FramesOverlap,

        /// The queue was held as far behind the host as the run asked, by the hold's own zone
        /// over the measured frames. Asked of a run that asked for a hold: the hold is a premise
        /// of what such a run measures, and one that came out short is a run of something else —
        /// a count taken once at the card's idle clock held a fifth of what was asked, under the
        /// barrier gate, and said so nowhere but in a figure among twenty.
        QueueHeld,

        /// No frame wrote a NaN or an infinity into a history or handed one to the denoiser,
        /// summed over the measured frames at every boundary `Rtx::NotFinite` names. A history
        /// keeps one and spreads it, so the picture goes black in blocks; a froxel that took
        /// `0 / 0` once did that to a whole night, and nothing on the way refused it.
        Finite,
    };

    /// What a check is called on a command line and in a report.
    std::string_view checkName(Check check);

    /// Every check there is, in the order they are run.
    std::span<const Check> everyCheck();

    struct Stop;

    /// Whether a place staged as `stop` can answer `check` at all, which is a different question
    /// from whether it passes. Beside the name in one table, so a new check says both.
    bool canAsk(Check check, const Stop& stop, const Rtx::RenderProfile& profile);

    struct Approach;

    /// Where a stop stands: a cell, and where the eye is inside it.
    struct Stand
    {
        /// The cell to teleport to, as Morrowind addresses one: a pair of integers is an exterior,
        /// anything else is an interior's name. Empty stays wherever the game already is. The
        /// spelling and not an id, because `MWBase::World::findExteriorPosition` is what turns one
        /// into the other, and a stop then stands where a player typing `coc` would.
        std::string mCell{};

        /// Where the eye goes and what it looks at. Both left out leaves the player where the cell
        /// put them and their own camera alone.
        std::optional<osg::Vec3f> mEye{};
        std::optional<osg::Vec3f> mLook{};

        /// The point the eye faces: `mLook`, or due north where it names nothing or the eye itself,
        /// because a direction of no length aims nothing. One answer, because `CameraDriver`
        /// aims at it and `Check::CameraStands` asserts the camera reached it. Only for a stand that
        /// names an eye.
        osg::Vec3f getLook() const;

        /// Faces the eye at `look`, and answers whether there was an eye to face: a stand with none
        /// puts the player where the cell puts one, facing where it faces them, so a look over it
        /// would be dropped without a word.
        bool lookAt(const osg::Vec3f& look);

        /// The same facing as the game's own rotation of a body, in radians: `(pitch, 0, yaw)` in
        /// the order `ESM::Position::rot` keeps them, yaw clockwise from north and pitch negative
        /// looking up, which is what `MWRender::Camera` negates into its own angles. The one
        /// derivation of a facing, so a body stood by a stand and the bearing a stand prints cannot
        /// disagree.
        osg::Vec3f getRotation() const;

        /// The unit vector a body with `rotation` faces, which is `getRotation`'s inverse: a stand
        /// read back off a body the harness rotated by a stand is that stand.
        static osg::Vec3f forwardOf(const osg::Vec3f& rotation);

        /// The unit vector along the level of the yaw: the way a player facing like this stand walks.
        osg::Vec3f getLevelAhead() const;

        /// A flight into this stand from `left` units to its left and `behind` units behind it,
        /// facing its look all the way, arriving at the last of `frames` frames of `step` seconds,
        /// through a world held still: what `noise --strafe` and `--walk` take a frame at the end
        /// of, with the history the eye moved through. Both along the level of the yaw, as a player
        /// walks, so a stand looking straight down flies along its bearing. A negative `behind`
        /// starts in front of the stand and walks back. Only for a stand that names an eye, and for
        /// two frames or more.
        Approach approachFrom(float left, float behind, float step, std::uint32_t frames) const;
    };

    /// What the sky does at a stop, asked of the game's own weather system rather than derived.
    /// Named for the stop, because `Sky` is a namespace `Rtx` names.
    struct StopSky
    {
        std::optional<float> mHour{};

        /// Which day of Morrowind's calendar, counted from the one a new game begins on. Only the
        /// moons read it: a phase runs on a three-day cycle and no hour can stand for a date.
        std::optional<int> mDay{};

        /// A weather as the content files spell it: `Clear`, `Overcast`, `Thunderstorm`. Set
        /// immediately, so a stop stands under it from its first frame.
        std::optional<std::string> mWeather{};

        /// Where the air's clocks stand at the stop's first counted frame, or nothing to leave them
        /// wherever the session's frames carried them. The fog drifts and churns on these and the
        /// deck scrolls on them, so without it a place is drawn under the air of however many
        /// frames the run happened to have drawn: a window flown for an hour stands in one fog and
        /// a run started fresh stands in another. Held until that frame and free after it, as
        /// `MWRender::RtxRun::getHeldAir` says.
        std::optional<Rtx::AirClock> mAir{};

        /// Weathers to turn the sky through while the stop runs, in order and round again, as
        /// transitions: what the renderer has to survive is an emitter freed on an ordinary frame.
        /// Asking for it stops the run being a benchmark.
        std::vector<std::string> mTurnThrough{};
    };

    /// Where a stop flies to, and how fast. A route is what puts a cell arriving into a
    /// measurement at all. The view file states the first three or none, `loadViews`.
    struct Route
    {
        /// Where the eye ends and what it looks at there.
        osg::Vec3f mTo;
        osg::Vec3f mLookTo{};

        /// World units a second, more than nought. A Morrowind exterior cell is 8,192 across.
        float mSpeed = 0.0f;

        /// Whether the world stays as the stop holds it while the eye flies: a flight a command
        /// stages into a place a still reference stands at (`Stand::approachFrom`). False for a
        /// view's route, which walks through a living world — the route `VerbPolicy::mFliesRoutes`
        /// speaks of.
        bool mWorldHeld = false;
    };

    /// Where a flight into a stand leaves from, and the route it flies: `Stand::approachFrom`.
    struct Approach
    {
        Stand mFrom;
        Route mRoute;
    };

    /// How long a stop runs and what moves while it does.
    struct Schedule
    {
        /// How long it runs and how much of it is thrown away first.
        BenchSpec mSpec{};

        std::optional<Route> mRoute;

        /// The flight a film's take makes through its keys, frame by frame over the measured
        /// frames, with the hour and the sky it runs under: what a take has in place of a route.
        /// The warm-up stands at its first frame.
        std::optional<CameraTrack> mTrack{};

        /// How many differently-seeded frames to average into one picture, or nought for none. A
        /// converged reference is the only ground truth a sampled renderer has: error falls as the
        /// square root of this, a hundred is a clean picture and a thousand is a reference.
        std::uint32_t mAccumulate = 0;

        /// Where the stop's frames start in the sampler's sequence: added to the count of its
        /// frames the renderer samples by (`MWRender::RtxRun::getSampleFrame`). Two stops at one
        /// place, the world held, draw the same samples frame for frame unless this sets them
        /// apart, and then what was to be a mean of independent draws is one draw again.
        std::uint32_t mSampleOffset = 0;

        /// What every frame of the stop asks of the reconstruction and of the exposure in place of
        /// the profile's, or nothing for the profile's: `MWRender::RtxRun::getReconstruction`.
        std::optional<Rtx::ReconstructionRequest> mReconstruction{};
        std::optional<Rtx::ExposureRule> mExposure{};

        /// What the stop upscales by in place of the run's mode, or nothing for the run's:
        /// `MWRender::RtxRun::getUpscale`.
        std::optional<Rtx::Upscale> mUpscale{};

        /// Whether the world's clock is held still while the stop runs, so a still frame traced
        /// many times is the same frame. `DateTimeManager::setSimulationTimeScale` is where it
        /// lands, so nothing in the world moves.
        bool mFrozen = false;

        /// Whether the player keeps their own camera and collision: a session somebody flies,
        /// `VerbPolicy::mPlayed`. A view's coordinates are where a camera stands and not where a
        /// body fits, so the walls come off with it.
        bool mFreeCamera = false;
    };

    /// What a stop writes, and when.
    struct Actions
    {
        /// Where the last measured frame is written as a PNG, or empty for none.
        std::filesystem::path mCapture;

        /// A mean the last measured frame is added to, which the stop that adds the `mOf`th writes
        /// to `mFile` as a PNG: what a picture converges to, which no one stop draws. The stops one
        /// mean takes run one after another.
        struct Mean
        {
            std::filesystem::path mFile;
            std::uint32_t mOf = 0;
        };
        std::optional<Mean> mMean;

        /// Where every measured frame's figures are written, a frame a line, or empty for none:
        /// `writeFrameTimes` says what for.
        std::filesystem::path mFrameTimes;

        /// Whether the scene the renderer was handed is reported: what it holds, what it could not
        /// place, and one number for the whole of it.
        bool mDigest = false;

        /// Whether the scene report says what a second walk of the same graph added. The largest
        /// cost a frame has, so only a stop that asked pays for it: this, or `Check::WalkTwice`.
        bool mWalkTwice = false;

        /// Where every texture the scene holds is written, vanilla beside de-lit, as one sheet.
        std::filesystem::path mSheet;

        /// Where one local-map tile of the place is written, framed the way the game's own compass
        /// frames one.
        std::filesystem::path mMapTile;

        /// Whose inventory doll to write, and where: a picture of a subject and not of the world,
        /// assembled by `MWRender::NpcAnimation` and traced against a scene of its own.
        struct Doll
        {
            std::string mWho;
            std::filesystem::path mFile;
        };
        std::optional<Doll> mDoll;

        /// A word to look for among the textures the scene around this place is wearing — what a
        /// frame would trace, rather than what the content files say stands near.
        std::string mFind;

        /// Where every measured frame is written as a numbered PNG, a film's frames: the directory,
        /// and the number the stop's first measured frame takes, so a take's frames follow the take
        /// before's whatever order they come back in.
        struct Film
        {
            std::filesystem::path mDirectory;
            std::uint32_t mFirst = 0;
        };
        std::optional<Film> mFilm;

        /// Whether every measured frame is read back and hashed, which serialises every frame
        /// against the device and so stops the run being a benchmark.
        bool mHash = false;

        /// What this stop asserts. Empty for a stop that only draws.
        std::vector<Check> mChecks;

        /// Whether the graph is walked a second time: for the report, or for the check that asks
        /// what the walk added.
        bool walksTwice() const;
    };

    /// One place a run visits, and everything that is true of it.
    struct Stop
    {
        /// What the report and the hashes call it. A view id where the run came from a view file.
        std::string mName{};

        /// What the report prints beside the name.
        std::string mNote{};

        Stand mStand{};
        StopSky mSky{};
        Schedule mSchedule{};
        Actions mActions{};
    };

    /// A whole run, as one description, filled by a launcher and read by the renderer.
    struct SessionRequest
    {
        std::vector<Stop> mStops;

        /// What the renderer is made with: the command's (`Framed::mSetup`), hidden and stepped unless
        /// it says otherwise.
        MWRender::RunSetup mSetup;

        /// Whether somebody plays the session (`VerbPolicy::mPlayed`): the menus are theirs to open
        /// and close. Otherwise the run closes any a script opens, and draws the interface only
        /// where `mHud` asks.
        bool mPlayed = false;

        /// Whether the game's HUD is drawn over the picture, and for a session nobody plays whether
        /// anything of the interface is. Off by default: a picture is of the world, and the bars and
        /// the compass are the played game's.
        bool mHud = false;

        /// Whether the played game's vanity camera may take over — the orbit round the player
        /// after `fVanityDelay` seconds with nobody at the keys. Off by default: a run is idle by
        /// nature, and a window somebody is reading must not start turning on its own.
        bool mVanity = false;

        /// Whether the run ends the session when its last stop does. False is a window somebody
        /// keeps flying after the schedule has run out.
        bool mQuitAtEnd = true;

        /// What the world's random draws are seeded with at every stop's first frame, so a stop's
        /// world is a function of the seed and the frames. The engine is seeded with it once at
        /// start, and what the start consumes before a stop begins is not a count of frames: it
        /// moved with the cache the run found and the environment it ran in, and every wandering
        /// body then stood elsewhere from the first measured frame.
        unsigned int mRandomSeed = 0;

        /// Where the run is written as a record, and the hashes it writes and compares. Empty
        /// where none was asked for.
        std::filesystem::path mJson;
        std::filesystem::path mHashes;
        std::filesystem::path mAgainst;

        /// Where every hashed frame's picture is written as well, or empty to keep only the hash:
        /// what a pair that differed is diffed pixel by pixel from, since a hash says which frame
        /// and never where in it.
        std::filesystem::path mPictures;

        /// Where a window appends a film's key on every Home press, or empty for none.
        std::filesystem::path mKeys;

        /// Where a window writes the picture of every Home press, the press's block inside it.
        std::filesystem::path mHomePictures;

        /// perf's control fifo, or empty where the run is not being profiled.
        std::filesystem::path mPerfControl;

        /// Which suite the stops came from, for the record's own header.
        std::string mSuite;
    };

    /// What a launcher reads back once `Engine::go` has returned.
    struct SessionResult
    {
        /// Non-zero where a hashed run differed from its reference, or where a stop could not be
        /// reached at all.
        int mExitStatus = 0;

        std::vector<BenchPlace> mPlaces;

        /// What the run printed, whole, for a launcher whose output is read rather than logged.
        std::string mReport;

        /// Where the run was left, as a stop that would put a camera back there, so a place somebody
        /// flew to and closed the window on is not lost. Nothing where no stop began.
        std::optional<Stop> mLeft;
    };

}
