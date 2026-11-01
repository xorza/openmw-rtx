#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "benchrun.hpp"

namespace RtxTool
{
    /// A list of places to profile, by view id and not by coordinates, so the frame a screenshot
    /// shows and the frame a number was measured on are the same frame.
    struct BenchSuite
    {
        std::string mName;
        std::string mNote{};

        /// In the order they were written, which is the order they are run in.
        std::vector<std::string> mViews{};

        /// Whether each hand-over waits for the distant ground it collects, or nothing to let the
        /// frame clock decide: `MWRender::RunSetup::mSettled`. A suite that times the streaming
        /// path says no, because waiting is most of what that path then measures — and a run
        /// under it may not be compared with a picture.
        std::optional<bool> mSettled{};
    };

    /// Reads the suite file. Throws when it is missing or malformed, rather than quietly profiling
    /// somewhere else.
    std::vector<BenchSuite> loadSuites(const std::filesystem::path& path);

    /// The suite called `name`, or null.
    const BenchSuite* findSuite(const std::vector<BenchSuite>& suites, std::string_view name);

    /// One stop from a view file entry and the sky the command line named. A view id names one
    /// frame, so a place measured at dawn says so in `mSky.mHour`; the command line still wins, as
    /// it does for `pos` and `look` — for the hour, the weather and the air, each where it names
    /// one. What comes back has the hour and the weather settled, so nothing downstream asks which
    /// won. The day is the line's, since a view names none, and is only for the moons; what the
    /// sky is turned through is not a condition of the place and stays the caller's.
    Stop stopFor(const Stop& view, const StopSky& given);

    /// Reads the view file. Throws when it is missing or malformed, rather than quietly rendering
    /// somewhere else.
    std::vector<Stop> loadViews(const std::filesystem::path& path);

    /// The view called `name`, or null.
    const Stop* findView(const std::vector<Stop>& views, std::string_view name);

    /// The view called `name`. Throws where there is none, in the one words every verb refuses an
    /// unknown view with.
    const Stop& requireView(const std::vector<Stop>& views, std::string_view name);

    /// The views `named` asks for, in the order it names them; every one where it names none or
    /// "all". Throws naming a view that is not there. One place, because `bench` reaches it through
    /// a suite and `shot` directly, and a run of one has to be reproducible with the other.
    std::vector<Stop> chooseViews(const std::vector<Stop>& views, const std::vector<std::string>& named);
}
