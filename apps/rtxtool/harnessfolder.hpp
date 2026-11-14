#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <components/rtx/renderer/shaderset.hpp>
#include <components/rtxvulkan/spirv/shaderfolder.hpp>

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

    /// The shaders `verb` reads, and what holds them: the ones the game compiles in, or one of the harness's
    /// other sets, read from its folder — the same modules compiled again with their source in them where
    /// `source` asks (for a profiler), and with the census for every verb but one that measures
    /// (`VerbPolicy::mMeasures`), whose figures are of the game's kernels. `RTX_SPIRV_DIR` says why the source
    /// is a set apart.
    class HarnessShaders
    {
    public:
        /// @throws Rtx::InputError where a folder's set cannot be read.
        HarnessShaders(Verb verb, bool source);

        /// Pinned: a folder's set points into what this holds.
        HarnessShaders(const HarnessShaders&) = delete;
        HarnessShaders& operator=(const HarnessShaders&) = delete;
        HarnessShaders(HarnessShaders&&) = delete;
        HarnessShaders& operator=(HarnessShaders&&) = delete;
        ~HarnessShaders() = default;

        const Rtx::ShaderSet& getSet() const;

        /// The set's name, which its driver cache is named after (`DriverCache`): its folder's.
        std::string_view getName() const { return mName; }

    private:
        std::string mName;
        std::optional<Rtx::ShaderFolder> mFolder;
    };
}
