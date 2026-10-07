#pragma once

#include <filesystem>

#include <components/rtx/renderer/shaderdirectory.hpp>

#include "verbs.hpp"

namespace RtxTool
{
    /// The harness's own folder in the build tree that built it, `RTX_HARNESS_DIR`: its places and
    /// suites, its scripts, its shaders and the driver's caches. Outside any bundle and named by no
    /// install rule, so no package carries the harness. Each folder and file here is the build's
    /// (`RTX_HARNESS_DIR` and its siblings), which names it once.
    std::filesystem::path harnessDirectory();

    /// The places a run can visit, `views.cfg` in the harness's folder.
    std::filesystem::path viewsFile();

    /// The suites, each a list of places in `viewsFile`: `benches.cfg` in the harness's folder.
    std::filesystem::path suitesFile();

    /// The data directory a watched run adds for the keys it answers to, and the played game never
    /// does.
    std::filesystem::path keysDirectory();

    /// The shaders `verb` reads: the game's under `resources`, or one of the harness's three other
    /// sets — the same modules compiled again with their source in them, where `source` asks, for a
    /// profiler that shows a shader's lines, and with the census, for every verb but one that
    /// measures (`VerbPolicy::mMeasures`), whose figures are of the game's kernels. `RTX_SPIRV_DIR`
    /// says why the source is a set apart.
    Rtx::ShaderSet shadersFor(const std::filesystem::path& resources, Verbs verb, bool source);
}
