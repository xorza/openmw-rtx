#pragma once

#include <apps/rtxtool/model/exitstatus.hpp>

#include "plans.hpp"

namespace RtxTool
{
    /// Runs `run` against a real game, presented as its window asks, and gives back what it came to.
    /// A real game and not a world of this tool's own, which would never pay for the whole-graph walk, the
    /// sweep or a cell arriving. The engine is built as `apps/openmw/main.cpp` builds one, after the window
    /// is written into the settings it reads. A played run prints where the eye was left as a `views.cfg`
    /// block.
    SessionResult runHosted(const Command& command, HostedRun run);
}
