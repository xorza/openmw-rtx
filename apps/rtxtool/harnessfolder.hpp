#pragma once

#include <filesystem>

#include <components/rtx/renderer/shaderdirectory.hpp>

#include "verbs.hpp"

namespace RtxTool
{
    /// The harness's own folder in the build tree that built it, `RTX_HARNESS_DIR`: its places and
    /// suites, its scripts, its shaders and the driver's caches. Outside any bundle and named by no
    /// install rule, so no package carries the harness.
    std::filesystem::path harnessDirectory();

    /// The shaders `verb` reads: the game's under `resources`, or one of the harness's three other
    /// sets — the same modules compiled again with their source in them, where `source` asks, for a
    /// profiler that shows a shader's lines, and with the census, for every verb but one that
    /// measures (`VerbPolicy::mMeasures`), whose figures are of the game's kernels. `RTX_SPIRV_DIR`
    /// says why the source is a set apart.
    Rtx::ShaderSet shadersFor(const std::filesystem::path& resources, Verbs verb, bool source);
}
