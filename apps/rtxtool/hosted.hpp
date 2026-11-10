#pragma once

#include <apps/rtxtool/model/benchrun.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>

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
        const Rtx::ShaderSet& mShaders;
        Verbs mVerb;
        const ShippedDefaults& mShipped;
    };

    /// Runs `request` against a real game, presented as `window` asks, and gives back a process exit
    /// status. The game and not a world of this tool's own, because a staged world never pays for
    /// the whole-graph walk, the sweep or a cell arriving, and stands in a world nobody plays. The
    /// engine is built exactly as `apps/openmw/main.cpp` builds one, out of the line's game, after
    /// the window is written into the settings it reads. A played run prints where the eye was left
    /// as a `views.cfg` block.
    int runHosted(const Command& command, const Framed& framed, SessionRequest request);
}
