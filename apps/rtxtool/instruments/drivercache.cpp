#include "drivercache.hpp"

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include <components/platform/process.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>

#include "digest.hpp"

namespace RtxTool
{
    namespace
    {
        /// Far past what one set of these shaders takes, which is under a hundred megabytes: the
        /// size the driver prunes a cache at. In bytes for NVIDIA's variable, and in gibibytes, the
        /// unit Mesa's `MESA_SHADER_CACHE_MAX_SIZE` reads a bare `G` as, for Mesa's.
        constexpr std::uint64_t sDriverCacheGibibytes = 8;
        constexpr std::uint64_t sDriverCacheBytes = sDriverCacheGibibytes << 30;
    }

    DriverCache::DriverCache(const std::filesystem::path& shaders)
        : mRoot(shaders.parent_path() / (shaders.filename().string() + "-driver-cache"))
        , mDirectory(mRoot / spellHash(Rtx::digestShaders(shaders)))
    {
        std::filesystem::create_directories(mDirectory);
    }

    void DriverCache::applyToDriver() const
    {
        Platform::Process::setEnvironment("__GL_SHADER_DISK_CACHE", "1");
        Platform::Process::setEnvironment("__GL_SHADER_DISK_CACHE_PATH", mDirectory.string().c_str());
        Platform::Process::setEnvironment("__GL_SHADER_DISK_CACHE_SIZE", std::to_string(sDriverCacheBytes).c_str());
        Platform::Process::setEnvironment("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP", "1");

        // Mesa's, for RADV, ANV and NVK: its cache goes in a directory of its own name under this
        // one, so the two drivers' files never meet.
        Platform::Process::setEnvironment("MESA_SHADER_CACHE_DISABLE", "false");
        Platform::Process::setEnvironment("MESA_SHADER_CACHE_DIR", mDirectory.string().c_str());
        Platform::Process::setEnvironment(
            "MESA_SHADER_CACHE_MAX_SIZE", (std::to_string(sDriverCacheGibibytes) + "G").c_str());
    }

    void DriverCache::sweep() const
    {
        // Gathered before anything is removed, because an iterator over a directory being emptied
        // may or may not visit what follows the removal.
        std::error_code failed;
        std::vector<std::filesystem::path> outdated;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(mRoot, failed))
            if (entry.path() != mDirectory)
                outdated.push_back(entry.path());

        for (const std::filesystem::path& gone : outdated)
            std::filesystem::remove_all(gone, failed);
    }
}
