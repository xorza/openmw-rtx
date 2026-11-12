#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include <volk.h>

#include <components/misc/digestwords.hpp>
#include <components/misc/result.hpp>

#include "owned.hpp"

namespace Rtx
{
    /// Where a pipeline cache is kept.
    struct PipelineCacheSpec
    {
        /// The directory the file goes in, made if it is not there. Empty keeps no cache at all, file
        /// or `VkPipelineCache` object, and compiles every stage from source: every measuring
        /// process, for the reason `RtxRenderer` gives where it leaves this empty.
        ///
        /// No object, not merely no file: pipelines are created in parallel against a shared
        /// object, and a stage two of them share is compiled by whichever asked first and handed
        /// back to the other, which is not the code a fresh compile of the same SPIR-V makes. Which
        /// pipeline got which was timing, and a third of the runs of one scene drew a pixel a part
        /// in 255 apart on one frame in two hundred; none of twelve did without the object.
        std::filesystem::path mDirectory;
    };

    /// A `VkPipelineCache` that outlives the process, kept in a file in the user's cache directory,
    /// so an edited shader is compiled once rather than once per process. Nothing here is
    /// allowed to fail loudly: a cache that cannot be read or written means compiling from
    /// scratch and nothing worse.
    class PipelineCache
    {
    public:
        /// @param device the handle pipelines will be created on.
        /// @param properties identifies the driver the cache was built by. Vulkan will reject a blob
        ///        that does not match, and the name carries it so that a driver update starts a new
        ///        cache instead of rejecting the old one on every run.
        /// @param shaderDirectory the compiled shaders the pipelines are built from, digested into the
        ///        file's name.
        PipelineCache(VkDevice device, const VkPhysicalDeviceProperties& properties, const PipelineCacheSpec& spec,
            const std::filesystem::path& shaderDirectory);
        ~PipelineCache();

        /// Null where the spec keeps no cache or the cache could not be created, which every
        /// `vkCreate*Pipelines` accepts as "no cache" — so a caller passes this without asking.
        VkPipelineCache getHandle() const { return mHandle.get(); }

        /// Writes the driver's current blob back where it differs from what was loaded or last
        /// written, through a temporary and a rename — or deletes the file where the blob has
        /// outgrown `sMostBytes`. Nothing where the spec keeps no cache. Called once the start's
        /// pipelines are made as well as at destruction, so a crash does not lose the whole compile.
        void save();

        /// The most a blob may hold before a run throws it away — a backstop, not the eviction. Well
        /// clear of the largest live set, since a cap that trips on a working cache drops it every run.
        static constexpr std::size_t sMostBytes = std::size_t{ 256 } << 20;

        /// Whether a stored blob is one this driver wrote and small enough to keep, and why not where
        /// it is not. Checked here as well as by the driver, because the file is untrusted data.
        /// Public because an offset off by four would reject every blob with no symptom but a cache
        /// that never hit.
        static Result<void, std::string_view> accepts(
            std::span<const std::uint8_t> blob, const VkPhysicalDeviceProperties& properties);

        /// The blob the file at `path` holds, where its own header says the device, driver, length
        /// and digest this run has and `accepts` takes it; nothing where there is no file; and why it
        /// was set aside otherwise. Public for the reason `accepts` is.
        static Result<std::vector<std::uint8_t>, std::string_view> read(
            const std::filesystem::path& path, const VkPhysicalDeviceProperties& properties);

    private:
        /// Removes the oldest of this renderer's other caches in the directory, past the few it
        /// keeps — the eviction. A partial write ends with the same suffix, so one left by a killed
        /// process ages out like any other cache; one swept in that window costs its writer a compile.
        void sweep() const;

        VkDevice mDevice = VK_NULL_HANDLE;

        /// The device the file is written for, which its header names.
        VkPhysicalDeviceProperties mProperties;

        /// Immediate, because the device takes its graveyard apart first.
        Immediate<VkPipelineCache, vkDestroyPipelineCache> mHandle;
        std::filesystem::path mPath;

        /// What was loaded or last written, by length and digest so a run that compiled nothing new
        /// rewrites nothing. Not the blob: tens of megabytes held all session for one comparison.
        std::size_t mKeptBytes = 0;
        Misc::DigestWords mKeptDigest{};
    };
}
