#pragma once

#include <filesystem>

#include <components/rtx/common/digestwords.hpp>

namespace Rtx
{
    /// A directory of compiled shaders, and whether its modules count their stores that were not
    /// finite into the census (`lib/census.glsl`): one value, because the two cannot disagree — a
    /// census module binds what a device that does not count lays out nowhere, and a device that
    /// counts handed modules that do not would count nothing and say nought.
    struct ShaderSet
    {
        std::filesystem::path mDirectory;
        bool mCensus = false;
    };

    /// Where a renderer reads its compiled shaders under `resources`: the modules with their source
    /// taken out, which is what the driver's caches are keyed on. A harness can hand a renderer the
    /// same modules with their source instead, for a profiler that shows a shader's lines.
    std::filesystem::path shaderDirectory(const std::filesystem::path& resources);

    /// A number that changes when any compiled shader in `directory` does, and with nothing else:
    /// every file's name and bytes, in name order. The whole directory, because a module none of
    /// one run's pipelines named may be named by the next. The modules without their source are
    /// under a megabyte and hash in a fifth of a millisecond once the system has read them, and the
    /// eight megabytes with it in one; nought where the directory cannot be read.
    DigestWords digestShaders(const std::filesystem::path& directory);
}
