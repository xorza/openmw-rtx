#pragma once

#include <filesystem>

#include <boost/program_options/variables_map.hpp>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/options.hpp>
#include <apps/rtxtool/verbs.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>

#include "framed.hpp"

namespace Files
{
    struct ConfigurationManager;
}

namespace RtxTool
{
    /// What every command is handed: the line it was given, the configuration that line was
    /// read against, where the resources are, and which of their shader sets a renderer reads —
    /// the one the driver's cache was pointed at.
    struct Command
    {
        const boost::program_options::variables_map& mVariables;
        const ToolOptions& mOptions;
        Files::ConfigurationManager& mConfig;
        const std::filesystem::path& mResources;
        const Rtx::ShaderSet& mShaders;
        Verbs mVerb;
        const ShippedDefaults& mShipped;
    };

    /// Runs `request` against a real game, presented as `window` asks, and gives back a process exit
    /// status. The game and not a world of this tool's own, because a staged world never pays for
    /// the whole-graph walk, the sweep or a cell arriving, and stands in a world nobody plays. The
    /// engine is built exactly as `apps/openmw/main.cpp` builds one, out of `variables`, after the
    /// window is written into the settings it reads. A played run prints where the eye was left as
    /// a `views.cfg` block.
    int runHosted(const Command& command, const Framed& framed, SessionRequest request);
}
