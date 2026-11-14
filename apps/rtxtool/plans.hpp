#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <apps/rtxtool/model/benchrun.hpp>
#include <components/rtx/renderer/shaderset.hpp>

#include "compare.hpp"
#include "framed.hpp"
#include "toolline.hpp"
#include "verbs.hpp"

namespace Files
{
    struct ConfigurationManager;
}

namespace RtxTool
{
    /// What every command is handed: the line it was given, read once (`ToolOptions::read`), the
    /// configuration that line was read against, and which of the resources' shader sets a renderer
    /// reads — the one the driver's cache was pointed at.
    struct Command
    {
        const ToolLine& mLine;
        Files::ConfigurationManager& mConfig;

        /// The run's own directory (`ownConfigDirectory`), which its saves go inside.
        const std::filesystem::path& mOwnDirectory;

        const Rtx::ShaderSet& mShaders;
        Verb mVerb;
        const ShippedDefaults& mShipped;
    };

    /// A run as a verb hands it to the game: the window it is presented in, and everything else the session reads.
    struct HostedRun
    {
        WindowRequest mWindow;
        SessionRequest mRequest;
    };

    /// Where a verb writes its pictures: `--out`, or a directory named for the verb.
    std::filesystem::path outOf(const Command& command);

    /// Every view a run names, settled: a condition named on the command line is every place's, and none of them
    /// keeps its own.
    std::vector<Stop> stopsFrom(const std::vector<Stop>& views, const ToolLine& line, const Framed& framed);

    /// The places a run of a suite visits, in the order it visits them, with what the suite says about them.
    struct SuiteRun
    {
        std::vector<Stop> mViews;

        /// Which suite, for the record's own header; empty where `--views` named the places.
        std::string mSuite;

        /// What the suite says about waiting for the ground, or nothing: `BenchSuite::mSettled`.
        std::optional<bool> mSettled;
    };

    /// `--views` beats `--suite`, and both name entries in `views.cfg`. Neither carries coordinates: those live
    /// with the view, so the frame a picture is taken of and the frame a number is measured on stay one frame.
    ///
    /// @param ownSuite the suite the verb runs when neither `--views` nor `--suite` names one: `bench` measures the
    ///        frame budget's places and `check` walks a route as well.
    SuiteRun chooseBenchViews(const ToolLine::Places& places, std::string_view ownSuite);

    /// The one place a `SessionRequest` is built: the stops as the command's row holds them, the setup the line
    /// framed, and what every run reads off the line besides. A verb that wants more — a suite, a file to write —
    /// says so on what comes back.
    ///
    /// **Takes the frame whole**, so the setup lives in the request alone from here on: a verb that changed its
    /// `Framed` after this would change a copy nothing reads.
    HostedRun sessionFor(const Command& command, Framed framed, std::vector<Stop> stops);

    /// Each verb's plan takes the line's `framed` whole (`frameFrom` in the executable) and changes it as the verb
    /// asks before `sessionFor` takes it.
    ///
    /// What `scene` runs: each place held still, walked twice, its scene digested or searched for `--find`.
    HostedRun planScene(const Command& command, Framed framed);

    /// What `shot` runs, and what it writes: every picture it asks for under `mOut`, each held to its rule.
    struct ShotPlan
    {
        HostedRun mRun;
        std::filesystem::path mOut;
        std::vector<WrittenPicture> mPictures;
    };

    /// What `shot` runs. The frame is judged by its hashes where they can judge it, and by its picture where the
    /// wavelet composed it and nothing upscaled it; a doll and a tile are always denoised; the sheet is the
    /// textures and nothing traced.
    ShotPlan planShot(const Command& command, Framed framed);

    /// What `bench` runs: the suite's places for its span, nothing written and nothing hashed unless asked. Throws
    /// for `--pictures` with no `--out` to write them into.
    HostedRun planBench(const Command& command, Framed framed);

    /// What `view` runs: the one place the line names, on the wall, until the window is closed or `--frames`
    /// have been drawn.
    HostedRun planView(const Command& command, Framed framed);

    /// What `check` runs: every check that can be asked at each place of its suite, under the queue held
    /// `sCheckHoldMs` behind the host unless the line names its own hold, and every picture a stop can write at
    /// the first place, into `outOf`.
    HostedRun planCheck(const Command& command, Framed framed);

    /// What a `shot` and a `bench --out` write their frames' hashes to, beside the pictures, and read a
    /// reference's from, in the directory `--against` names.
    inline constexpr std::string_view sRunHashes = "hashes.csv";
}
