#pragma once

#include <filesystem>

#include <components/rtx/common/digestwords.hpp>

namespace Rtx
{
    /// Where the harness keeps what no install carries, beside `resources`: its places and suites,
    /// its scripts, the shaders with their source and the driver's caches. No install rule names
    /// it, so a package is the game's resources and nothing of the harness's.
    std::filesystem::path harnessDirectory(const std::filesystem::path& resources);

    /// Where a renderer reads its compiled shaders: the modules with their source taken out, under
    /// `resources`, which is what the driver's caches are keyed on, or the same modules with it in
    /// the harness's directory, for a profiler that shows a shader's lines. `RTX_SPIRV_DIR` says why
    /// there are two.
    std::filesystem::path shaderDirectory(const std::filesystem::path& resources, bool withSource);

    /// A number that changes when any compiled shader in `directory` does, and with nothing else:
    /// every file's name and bytes, in name order. The whole directory, because a module none of
    /// one run's pipelines named may be named by the next. The modules without their source are
    /// under a megabyte and hash in a fifth of a millisecond once the system has read them, and the
    /// eight megabytes with it in one; nought where the directory cannot be read.
    DigestWords digestShaders(const std::filesystem::path& directory);
}
