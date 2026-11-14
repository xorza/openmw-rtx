#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/rtxrun.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/weather.hpp>

#include "benchrecord.hpp"
#include "benchspec.hpp"
#include "cameratrack.hpp"
#include "exitstatus.hpp"
#include "findingkind.hpp"
#include "pictureshelf.hpp"

namespace RtxTool
{
    /// How fast a measured run steps the world unless it states otherwise, in frames a second: world time and not
    /// wall time, so ten seconds is the same six hundred frames however fast the build draws them. Sixty because the
    /// frame budget is written against it. Never read in place of the step a run stated (`SessionRequest::mStep`).
    inline constexpr float sStepRate = 60.0f;

    /// How long one of those frames stands for, which is what a measured frame tells the renderer.
    inline constexpr float sStepSeconds = 1.0f / sStepRate;

    /// What one frame of world counts for where a run turns seconds into frames — a span, a flight,
    /// a turning sky: the stated step, or where the wall decides, the step a measured run states by
    /// default.
    inline float worldStep(const std::optional<float>& step)
    {
        return step.value_or(sStepSeconds);
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

        /// No static stands twice: nothing the cell ring stands off the content files is a reference the game has
        /// stood in its graph. The ring drops a cell's statics the frame the cell enters the active grid; one it
        /// kept would stand beside the game's, a building where the game put none.
        StaticsNotDoubled,

        /// Every gate that decided agrees with the game in the cells it has loaded: a reference behind an open gate
        /// is up and one behind a closed gate is down, as the reference's own script left it. A disagreement is a
        /// stage seen from afar that the cell changes as it loads.
        GatesAgree,

        /// Every texture the scene named could be read; an unreadable one is drawn grey.
        TexturesReadable,

        /// Nothing the world's mirror fills grew past the room it was made with, at any frame of the
        /// run (`MWRender::WorldMirror::countPastRoom`): a growth moves every entry on the frame a
        /// cell arrives, which the rooms exist to keep off the frame path.
        RoomsHeld,

        /// The frame was drawn from the camera the stop asked for, because a camera something else
        /// moved gives figures that look reasonable. Answered yes by a stop that named no camera,
        /// by a free-camera stop, and by a routed one.
        CameraStands,

        /// The world stood under the weather the stop named, crossing into none, at its last measured frame. Asked
        /// of a stop that names a weather and turns none: a world left to settle can drop the ask and draw a clear
        /// sky under `balmora-storm-night`, saying so nowhere but in the report's `in Clear`.
        SkyNamed,

        /// `Rtx::sFramesInFlight` frames were in flight at every submit. The ring is sized for them and the game
        /// keeps them, so a submit that found fewer is a wait somebody put back. Asked of a place that stands still,
        /// because an arrival drains the ring by design.
        FramesOverlap,

        /// The queue was held as far behind the host as the run asked, by the hold's own zone over the measured
        /// frames. Asked of a run that asked for a hold: it is a premise of what such a run measures, and one that
        /// came out short is a run of something else.
        QueueHeld,

        /// No shader stored a NaN or an infinity, summed over the measured frames by the module that wrote it
        /// (`Rtx::NotFinite`). A history keeps one and spreads it, so the picture goes black in blocks.
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

    /// A cell as Morrowind addresses one, read once from its spelling: a pair of integers is an exterior, anything
    /// else is an interior's name. The spelling and not an id, because the world's `findExteriorPosition` and
    /// `findInteriorPosition` turn one into the other, as for a player typing `coc`. One rule for whoever asks:
    /// `coc` would find an exterior by its name too, which nothing outside a loaded world can tell from an
    /// interior's, so a film planning its cuts before the world loads and the stager read the same cell.
    class CellAddress
    {
    public:
        /// No cell, which stays wherever the game already is.
        CellAddress() = default;

        /// `spelling` as `--cell` and a view's `cell` take it. An exterior is spelt again as `x,y`:
        /// the pair may have spaces around its integers, which the world does not read.
        explicit CellAddress(std::string_view spelling);

        static CellAddress exterior(int gridX, int gridY);

        /// An interior by its name, where the cell itself says it is one, whatever the name spells.
        static CellAddress interior(std::string_view name);

        bool empty() const { return mSpelling.empty(); }

        bool isExterior() const { return mExterior; }

        /// The pair `x,y` out of doors and the name indoors: what `--cell` takes and a view says.
        const std::string& getSpelling() const { return mSpelling; }

        bool operator==(const CellAddress&) const = default;

    private:
        std::string mSpelling;
        bool mExterior = false;
    };

    /// Where a stop stands: a cell, and where the eye is inside it.
    struct Stand
    {
        /// The cell to teleport to.
        CellAddress mCell{};

        /// Where the eye goes and what it looks at. Both left out leaves the player where the cell
        /// put them and their own camera alone.
        std::optional<osg::Vec3f> mEye{};
        std::optional<osg::Vec3f> mLook{};

        /// Whether the place's lamps light it. False where a view says `lamps = false`, which hides
        /// them through the seam's view mask as the game's own `Mask_Lighting` does, so a place lit
        /// by what glows in it can be judged by that light alone.
        bool mLamps = true;

        /// The weapon the player holds drawn, by its id as the content files spell it, seen through
        /// the player's own first-person eye, which stands at `mEye`. Empty where a view says no
        /// `arms`: the eye is a camera of its own, and the body is hidden from it.
        std::string mArms{};

        /// The point the eye faces: `mLook`, or due north where it names nothing or the eye itself,
        /// because a direction of no length aims nothing. One answer, because `CameraDriver`
        /// aims at it and `Check::CameraStands` asserts the camera reached it. Only for a stand that
        /// names an eye.
        osg::Vec3f getLook() const;

        /// Faces the eye at `look`, and answers whether there was an eye to face: a stand with none
        /// puts the player where the cell puts one, facing where it faces them, so a look over it
        /// would be dropped without a word.
        bool lookAt(const osg::Vec3f& look);

        /// The same facing as the game's own rotation of a body, in radians: `(pitch, 0, yaw)` in the order
        /// `ESM::Position::rot` keeps them, yaw clockwise from north and pitch negative looking up, which
        /// `MWRender::Camera` negates into its own angles. The one derivation of a facing, so a body stood by a
        /// stand and the bearing a stand prints cannot disagree.
        osg::Vec3f getRotation() const;

        /// The unit vector a body with `rotation` faces, which is `getRotation`'s inverse: a stand
        /// read back off a body the harness rotated by a stand is that stand.
        static osg::Vec3f forwardOf(const osg::Vec3f& rotation);

        /// The unit vector along the level of the yaw: the way a player facing like this stand walks.
        osg::Vec3f getLevelAhead() const;

        /// A flight into this stand from `left` units to its left and `behind` units behind it, facing its look all
        /// the way, arriving at the last of `frames` frames of `step` seconds through a world held still: what
        /// `noise --strafe` and `--walk` take a frame at the end of. Both along the level of the yaw, as a player
        /// walks; a negative `behind` starts in front of the stand and walks back. Only for a stand that names an
        /// eye, and for two frames or more.
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

        /// A weather, read once off a name by the parsers and named again only where text is
        /// printed. Set immediately, so a stop stands under it from its first frame.
        std::optional<Rtx::Weather> mWeather{};

        /// Where the air's clocks stand at the stop's first counted frame, or nothing to leave them wherever the
        /// session's frames carried them: the fog drifts and churns on these, so without it a place is drawn under
        /// the air of however many frames the run had drawn. Held until that frame and free after it
        /// (`MWRender::RtxRun::getHeldAir`).
        std::optional<Rtx::AirClock> mAir{};

        /// Weathers to turn the sky through while the stop runs, in order and round again, as
        /// transitions: what the renderer has to survive is an emitter freed on an ordinary frame.
        /// Asking for it stops the run being a benchmark.
        std::vector<Rtx::Weather> mTurnThrough{};

        /// The game clock's speed over the stop's measured frames as a multiple of the game's own, run by the world
        /// itself through its `timescale` as a player's game runs it, and held over the frames ahead of them; or
        /// nothing for the clock the stop's track or the world keeps. A tour's.
        std::optional<float> mClockTimes{};
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

        /// Whether the world stays as the stop holds it while the eye flies: a flight a command stages into a place
        /// a still reference stands at (`Stand::approachFrom`). False for a view's route, which walks through a
        /// living world (`VerbPolicy::mFliesRoutes`).
        bool mWorldHeld = false;
    };

    /// Where a flight into a stand leaves from, and the route it flies: `Stand::approachFrom`.
    struct Approach
    {
        Stand mFrom;
        Route mRoute;
    };

    /// **A length in samples a shown pixel**: as many frames, summed, as a frame of the run's own mode holds
    /// samples a shown pixel after `mAfter` frames of history, rounded down, at most `mMost` and never under one.
    /// The extents are the renderer's to give (DLSS's are NGX's, and an upscaler asked for as `auto` is chosen only
    /// as the renderer is made), so a stop states its length this way and the session settles it into frames
    /// (`SessionRequest::settleUnder`).
    struct HeldFrames
    {
        std::uint32_t mAfter = 0;
        std::uint32_t mMost = 0;

        /// The frames this comes to where the run's frames are traced and shown at `traced`.
        std::uint32_t under(const Rtx::FrameExtents& traced) const;

        bool operator==(const HeldFrames&) const = default;
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

        /// How many frames the stop measures and sums where that is the samples a frame of the run's mode holds,
        /// which no reader of the stop sees: `SessionRequest::settleUnder` fills `mSpec.mRun` and `mAccumulate`
        /// from it and clears it before the first frame.
        std::optional<HeldFrames> mHeld{};

        /// Where the stop's frames start in the sampler's sequence, added to the count the renderer samples by
        /// (`MWRender::RtxRun::getSampleFrame`). Two stops at one place, the world held, draw the same samples frame
        /// for frame unless this sets them apart, and a mean of independent draws is then one draw again.
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

        /// Whether the player keeps their own camera: a session somebody flies, `VerbPolicy::mPlayed`. A view's
        /// coordinates are where a camera stands and not where a body fits, so collision comes off with it.
        bool mFreeCamera = false;

        /// Whether the stop's flight goes on from the stop before's last frame, so it restages nothing: no cell, no
        /// eye and no sky set, and no warm-up, as the world, the histories and the camera stand where the last stop
        /// left them. A tour's turn between two rows (`TourLeg::mContinues`).
        bool mContinues = false;
    };

    /// What a stop writes, and when.
    struct Actions
    {
        /// Where the last measured frame is written as a PNG, and at what depth.
        struct Capture
        {
            std::filesystem::path mFile;

            /// The frame's bytes as shown, or the sum's own picture at sixteen bits a channel
            /// (`Rtx::Renderer::readDeepPixels`), undithered: a reference's, which a bias is measured
            /// against and whose byte would stand under it. Only a stop that sums
            /// (`Schedule::mAccumulate`) takes a deep one.
            enum class Depth
            {
                Shown,
                Deep,
            };
            Depth mDepth = Depth::Shown;
        };
        std::optional<Capture> mCapture;

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

        /// The exterior cells the stop must cover: each is covered once a frame stood whole while the player was in
        /// its column, and one the flight outran is a finding (`FindingKind::Outrun`). A tour's land leg's.
        std::vector<osg::Vec2i> mCovers;

        /// Where the stop stands in a soak: the first pass, or the same flight flown again at the run's end, whose
        /// figures are held against the first's (`FindingKind::Growth`). A tour's first land leg and its closing leg.
        enum class Soak : std::uint8_t
        {
            None,
            First,
            Again,
        };
        Soak mSoak = Soak::None;

        /// Whether the graph is walked a second time: for the report, or for the check that asks
        /// what the walk added.
        bool walksTwice() const;
    };

    /// What a stop whose place the content does not hold comes to (`Staging::stage`): it is passed over either way and
    /// the run goes on, as a test runner goes on past a test whose premise is missing.
    enum class WhenAbsent : std::uint8_t
    {
        /// The run fails at its end: somebody named the stop, so its absence is the content's fault or theirs.
        Fails,

        /// The run does not fail: the stop came of `--views=all`, which is every view the content can show.
        Skips,
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
        WhenAbsent mWhenAbsent = WhenAbsent::Fails;
    };

    /// What a run does with a stop that fails as it runs: the world that never stood whole, a pause nobody closed.
    enum class WhenAStopFails : std::uint8_t
    {
        /// The run ends there, failed: what it measured after would rest on a stop that broke.
        EndsTheRun,

        /// The stop is recorded as failed, the run fails at its end, and the next stop begins: a tour's leg, whose
        /// next is a place of its own.
        GoesOn,
    };

    /// A whole run, as one description, filled by a launcher and read by the renderer.
    struct SessionRequest
    {
        std::vector<Stop> mStops;

        /// What the renderer is made with: the command's (`Framed::mSetup`), hidden and stepped unless it says
        /// otherwise.
        MWRender::RunSetup mSetup;

        /// How long every frame stands for, in seconds, or nothing to time each one off the wall: what the run hands
        /// the engine's frame clock (`OMW::EngineHost::getFrameStep`), the one source the renderer reads it from.
        /// Everything the world animates steps by it, so two runs of one build are the same run. A window somebody
        /// watches wants the wall. A run's and never a setting's: a file that could state a step once would turn a
        /// played game into a fixed-step run for good.
        std::optional<float> mStep;

        /// Whether the command measures (`VerbPolicy::mMeasures`), which the report's header says.
        bool mMeasures = false;

        /// Whether somebody plays the session (`VerbPolicy::mPlayed`): the menus are theirs to open
        /// and close, and a model's companion maps are named by the player's patterns. Otherwise the
        /// run closes any a script opens, draws the interface only where `mHud` asks, and takes the
        /// shipped patterns, so two machines with the same content trace one scene; what a `_spec`
        /// map's channels mean is measured on each map (`Rtx::specularLayoutOf`).
        bool mPlayed = false;

        /// Whether the game's HUD is drawn over the picture, and for a session nobody plays whether
        /// anything of the interface is. Off by default: a picture is of the world, and the bars and
        /// the compass are the played game's.
        bool mHud = false;

        /// Whether the player's selected spell stands ready, and its effects on the hands, at a stop
        /// that names no weapon, in place of the hands put away.
        bool mSpellHands = false;

        /// Whether the played game's vanity camera may take over, the orbit round the player after `fVanityDelay`
        /// seconds with nobody at the keys. Off by default: a window somebody is reading must not start turning.
        bool mVanity = false;

        /// The magnitude of Night-Eye the player wears at every stop, as a constant effect would put
        /// it on them: what the game turns into `0.7 × magnitude / 100` over the ambient. Nought, the
        /// effect off, by default.
        int mNightEye = 0;

        /// Whether the run ends the session when its last stop does. False is a window somebody
        /// keeps flying after the schedule has run out.
        bool mQuitAtEnd = true;

        /// What the world's random draws are seeded with at every stop's first frame, so a stop's world is a
        /// function of the seed and the frames. The engine is seeded once at start, and what the start consumes
        /// before a stop begins is not a count of frames: it moved with the cache and the environment, and every
        /// wandering body then stood elsewhere from the first measured frame.
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

        /// Whether every picture a stop writes is kept as well, for the verb that judges them once the run is
        /// over (`SessionResult::mShelf`).
        bool mKeepsPictures = false;

        /// Where a window appends a film's key on every Home press, or empty for none.
        std::filesystem::path mKeys;

        /// Where a window writes the picture of every Home press, the press's block inside it.
        std::filesystem::path mHomePictures;

        /// Where a run that stops on a store not finite (`stopAt`) writes what the renderer kept of the frame
        /// that stored one (`Rtx::Renderer::takeNotFiniteFrame`): the verb's folder.
        std::filesystem::path mKeptFrames;

        /// perf's control fifo, or empty where the run is not being profiled.
        std::filesystem::path mPerfControl;

        /// Where the game's saves go (`ownSavesFolder`), or empty for the player's own.
        std::filesystem::path mSaves;

        /// Makes the run's stops where the world first stands, handed the world's own `timescale`, in place of
        /// `mStops`: a tour's, whose route is the content that loaded (`planTour`). Asked once, before any stop
        /// begins; none answered ends the run, passed.
        std::function<std::vector<Stop>(float ownTimeScale)> mPlanWhereTheWorldStands;

        /// Whether the run prints the stops it plans where the world stands and flies none (`tour --plan`), so
        /// nothing measured is what it was asked and no failure.
        bool mPlansOnly = false;

        WhenAStopFails mWhenAStopFails = WhenAStopFails::EndsTheRun;

        /// What a run that turns what it meets into findings fails on besides the faults (`FindingKind`), whether its
        /// frames are held to the step, which makes a slow frame the limiter's and no finding, and where the findings
        /// are written after every stop, so a run that dies says how far it came: a tour's.
        struct Watching
        {
            std::vector<FindingKind> mFailOn;
            bool mPaced = false;
            std::filesystem::path mFindings;
        };

        /// Whether the run watches for findings (`TourWatch`), or nothing.
        std::optional<Watching> mWatching;

        /// Which suite the stops came from, for the record's own header.
        std::string mSuite;

        /// Why the run stops at the frame `finished` answers for, or nothing where it goes on.
        /// **Where somebody plays the run, at the first frame that stored a value that was not finite**: a window
        /// that flies until it is closed measures nothing, so nothing after would say so, and what a NaN reaches it
        /// spreads to. Elsewhere the stop's end says it, where `Check::Finite` is asked.
        std::optional<std::string> stopAt(const Rtx::FrameResult& finished) const;

        /// What the renderer is made with: `mSetup`, with the run's upscaling mode and every one a
        /// stop takes as the modes it keeps room for (`RunProfile::mReservedUpscales`), each once
        /// and in order. A stop traced at another mode than the stop before then stands the world
        /// that stop stood, which a picture held against another stop's must.
        MWRender::RunSetup rendererSetup() const;

        /// Why the run cannot be traced under an upscaler that takes `input`, or nothing where it can:
        /// a stop that asks for the light unfiltered at a mode that upscales, which an upscaler that
        /// filters the light itself cannot give (`Rtx::rendererFilters`). Asked once the renderer has
        /// resolved its upscaler, before the first frame.
        std::optional<std::string> refusalUnder(Rtx::UpscalerInput input) const;

        /// Settles every stop whose length is samples a shown pixel (`Schedule::mHeld`) into frames under
        /// `traced`, what the renderer traces and shows the run's mode at. Asked once the renderer is made, before
        /// the first frame and before any stop's length is read.
        void settleUnder(const Rtx::FrameExtents& traced);
    };

    /// The hour and the weather a place stands under where neither the view nor the command line
    /// names one: what a picture of a place is taken at. Not the hour a budget is written against
    /// — a low sun doubles the trace — which is why the views the target is judged on fix `hour`.
    inline constexpr float sDefaultHour = 12.0f;
    inline constexpr Rtx::Weather sDefaultWeather = Rtx::Weather::Clear;

    /// How many frames a picture warms up over once the world stands whole, so it shows what a player standing
    /// there sees and every draw of it is a frame of its own: every command but `bench`, and a film's take after
    /// its cut. Four times the accumulator's length `N` (`ACCUMULATE_FRAMES`): its weight on the frame the cut left
    /// is `(1 - 1/N)^4N`, under `e^-4` whatever `N` is (1.7% at 32), and the air's `0.9^4N` is 1.4 in a million at
    /// 32. Derived rather than stated, so a longer accumulator lengthens it.
    inline const std::uint32_t sHistoryFrames = 4 * static_cast<std::uint32_t>(Rtx::Shaders::ACCUMULATE_FRAMES);

    /// Runs `stop` for `frames` once the world stood whole and its histories converged over
    /// `sHistoryFrames`, so its pictures are the ones a player standing there sees. Still where
    /// the command's row freezes the world (`VerbPolicy::mFreezes`), which `sessionFor` applies.
    ///
    /// @param frames how many to measure once the world has arrived. Why a command wants more
    ///        than one is that command's to say.
    void measureFrames(Stop& stop, std::uint32_t frames = 1);

    /// What a launcher reads back once `Engine::go` has returned.
    struct SessionResult
    {
        /// Failed where a stop somebody named could not be reached (`WhenAbsent::Fails`) or something else failed
        /// the run; else differed where a hashed frame differed from its reference; else passed.
        ExitStatus mExitStatus = ExitStatus::Passed;

        std::vector<BenchPlace> mPlaces;

        /// What the run printed, whole, for a launcher whose output is read rather than logged.
        std::string mReport;

        /// Where the run was left, as a stop that would put a camera back there, so a place somebody
        /// flew to and closed the window on is not lost. Nothing where no stop began.
        std::optional<Stop> mLeft;

        /// Every picture the stops wrote, where the request kept them (`SessionRequest::mKeepsPictures`): the
        /// result's and not the verb's, so nothing the verb holds is written into while the run lasts.
        PictureShelf mShelf;

        /// The extents the run's frames were traced and shown at, once the renderer made them, for the verb whose
        /// figures follow from them, as `noise`'s bar does. Nothing where no renderer was made.
        std::optional<Rtx::FrameExtents> mTraced;
    };

}
