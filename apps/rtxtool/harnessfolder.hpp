#pragma once

#include <filesystem>

namespace RtxTool
{
    /// The harness's own folder in the build tree that built it, `RTX_HARNESS_DIR`: its places and
    /// suites, its scripts, the shaders with their source and the driver's caches. Outside any
    /// bundle and named by no install rule, so no package carries the harness.
    std::filesystem::path harnessDirectory();

    /// The shaders with their source in them, for a profiler that shows a shader's lines: the same
    /// modules the renderer reads under the resources, compiled again without stripping. `RTX_SPIRV_DIR`
    /// says why there are two.
    std::filesystem::path shaderSourceDirectory();
}
