#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <osg/Vec3f>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/benchspec.hpp>
#include <apps/rtxtool/model/film.hpp>
#include <apps/rtxtool/model/maprules.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/multidircollection.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/weather.hpp>

namespace RtxTool
{
    /// An option's value, and whether the line or a configuration file named it rather than leaving
    /// it at its default: what a verb reads where it answers an option nobody named another way.
    template <class T>
    struct Named
    {
        T mValue{};
        bool mNamed = false;
    };

    /// `--content-cache=game`: the store the game keeps under the user's cache directory.
    struct GameContentCache
    {
        bool operator==(const GameContentCache&) const = default;
    };

    /// `--content-cache=off`: no store, so every pass runs.
    struct NoContentCache
    {
        bool operator==(const NoContentCache&) const = default;
    };

    /// What `--content-cache` names: the game's store, none, or a directory of the run's own.
    using ContentCacheChoice = std::variant<GameContentCache, NoContentCache, std::filesystem::path>;

    /// What `--size` names.
    struct FrameSize
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
    };

    /// The command line, read once into what each option means, after the configuration files
    /// filled what it left (`ToolOptions::read`): what every verb reads, in place of looking an
    /// option up by its name. An option a run may leave unsaid is optional, or `Named` where its
    /// default still stands for something; text that names nothing, `--pos=` or `--cell=`, is
    /// none. Every spelling is refused here, with the option that named it.
    struct ToolLine
    {
        /// What frames the world, which `frameFrom` makes a `Framed` of.
        struct Frame
        {
            FrameSize mSize;
            float mFieldOfView = 0.0f;
            Named<int> mDay;

            /// `--maps`, or where it is not given, nothing for the player's own under `view`, which
            /// is the played game, and `MapRules::Shipped` under every other verb.
            std::optional<MapRules> mMaps;

            Named<Rtx::Upscale> mUpscale;

            /// `--upscaler`: which backend runs, FSR by default so a measured picture does not
            /// change with the card it was measured on.
            Rtx::UpscalerChoice mUpscaler = Rtx::UpscalerChoice::Fsr;

            std::optional<bool> mGrass;
            std::optional<float> mDistantCells;
            std::optional<bool> mDistantStatics;
            std::optional<float> mGamma;

            /// `--memory-budget`, in bytes.
            std::optional<std::uint64_t> mMemoryBudget;

            std::filesystem::path mPipelineCode;

            /// `--content-cache`.
            ContentCacheChoice mContentCache{};

            Rtx::SurfaceView mShow{};
            Rtx::ExposureRule mExposure;
            Named<bool> mHold;
            bool mVariants = false;

            /// What each of `sReconstructionSwitches` says, over a request's own defaults.
            Rtx::ReconstructionRequest mReconstruction;

            /// Which of the bounces' switches the line named: a window takes the rest from the player's
            /// settings, where its menu moves them.
            struct BouncesNamed
            {
                bool mIndirect = false;
                bool mBounceRays = false;
            };
            BouncesNamed mBouncesNamed;
        };

        /// Where a run stands, or which places it visits.
        struct Places
        {
            /// `--view`, `--cell`, `--pos` and `--look`: the one place a line names.
            std::string mView;
            CellAddress mCell;
            std::optional<osg::Vec3f> mPos;
            std::optional<osg::Vec3f> mLook;

            /// `--views`, split, or nothing where the line names none: named with nothing in it, it
            /// is every view (`chooseViews`). And `--suite`.
            std::optional<std::vector<std::string>> mViews;
            std::optional<std::string> mSuite;

            /// `--load-savegame`, or empty for a new game.
            std::filesystem::path mSave;
        };

        /// The sky the line names over every place it stages.
        struct Sky
        {
            std::optional<float> mHour;
            std::optional<Rtx::Weather> mWeather;
            std::optional<Rtx::AirClock> mAir;

            /// `--turn-weather`, in its order.
            std::vector<Rtx::Weather> mTurn;
        };

        /// What every run reads off the line beside its places: `sessionFor`.
        struct Session
        {
            bool mHud = false;
            bool mSpellHands = false;
            bool mVanity = false;
            int mNightEye = 0;
            unsigned int mRandomSeed = 0;
        };

        /// How long a stop runs, and what `bench` writes beside its numbers.
        struct Bench
        {
            /// `--frames`, `--seconds`, and the span they come to, which only one of them names.
            std::uint32_t mFrames = 0;
            float mSeconds = 0.0f;
            BenchSpan mSpan;

            float mWarmup = 0.0f;
            bool mWindow = false;
            bool mCounters = false;
            std::filesystem::path mJson;
            std::filesystem::path mFrameTimes;
            std::filesystem::path mPerfControl;
            bool mPictures = false;
        };

        /// What `shot` draws besides the frame, and how often.
        struct Shot
        {
            std::uint32_t mAccumulate = 0;
            std::uint32_t mRepeat = 0;
            std::string mDoll;
            bool mMap = false;
            bool mTextures = false;
        };

        /// How `noise` takes its frame, and what its other side changes.
        struct Noise
        {
            float mStrafe = 0.0f;
            float mWalk = 0.0f;
            std::uint32_t mCut = 0;

            /// `--versus` as written, and the reconstruction its side runs with
            /// (`ToolOptions::versus`), or nothing where the line names no other side.
            std::string mAsked;
            std::optional<Rtx::ReconstructionRequest> mVersus;
        };

        /// What a film's line states.
        struct Film
        {
            bool mPlan = false;

            /// How the film is paced, as the line names it: the step one over `--fps`, and
            /// `--length` or `FilmPacing::sLengthByDefault` where neither it nor `--speed` is named.
            /// The frame's field of view and aspect, the day and the weathers' turn are the run's,
            /// set where the run is framed.
            FilmPacing mPacing;

            bool mEncode = false;
        };

        /// What the hosted game is started with, as `apps/openmw/main.cpp` reads it.
        struct Game
        {
            std::vector<std::string> mContent;
            Files::PathContainer mData;
            std::filesystem::path mDataLocal;
            std::vector<std::string> mFallbackArchives;
            Fallback::FallbackMap mFallback;
            std::vector<std::string> mGroundcover;
            std::string mEncoding;
        };

        bool mShaderSource = false;
        bool mFolders = false;
        std::filesystem::path mResources;

        /// `--out`, or nothing for a folder named after the verb.
        std::optional<std::filesystem::path> mOut;

        /// `--against`, `--keys` and `--find`: what a verb reads besides.
        std::filesystem::path mAgainst;
        std::filesystem::path mKeys;
        std::string mFind;

        /// The layers named on the line, demanded where named (`Rtx::ValidationOptions::mDemanded`).
        Rtx::ValidationOptions mValidation;

        Frame mFrame;
        Places mPlaces;
        Sky mSky;
        Session mSession;
        Bench mBench;
        Shot mShot;
        Noise mNoise;
        Film mFilm;
        Game mGame;
    };
}
