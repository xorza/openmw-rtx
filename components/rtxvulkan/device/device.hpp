#pragma once

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include <components/rtx/renderer/shaderdirectory.hpp>

#include "owned.hpp"
#include "physicaldevice.hpp"
#include "requirements.hpp"

namespace Rtx
{
    /// What a pipeline's compiler made it use: the statistics `VK_KHR_pipeline_executable_properties`
    /// reports under names each driver picks, of which AMD's own driver's are the ones read, and a
    /// launch's stack, which every driver answers. A figure the driver does not give is nothing.
    struct CompiledUse
    {
        /// The scratch memory a lane uses, in bytes — what the compiler spilled out of the
        /// registers, and any array indexed at run time: `scratchMemUsageInBytes`.
        std::optional<std::uint64_t> mScratchBytes;

        /// The vector registers a lane holds, which is how many waves share a SIMD's register file:
        /// `numUsedVgprs`.
        std::optional<std::uint64_t> mRegisters;

        /// A launch's ray generation stack, in bytes a lane, which every driver answers: where a
        /// launch keeps what does not fit its registers. **What a launch is measured by on AMD's
        /// own driver**, whose statistics for one describe a stub of three registers whatever the
        /// launch does. Nothing for a pipeline that is not a launch.
        std::optional<std::uint64_t> mStackBytes;

        /// The lanes a subgroup of the pipeline holds, the widest of its executables, which every
        /// driver that reports an executable answers: what a register count is a count of.
        std::optional<std::uint32_t> mSubgroupSize;
    };

    class BufferMarkers;
    class CommandPool;
    class Graveyard;
    class Instance;
    class MemoryAllocator;
    class NotFiniteCensus;
    class Timeline;
    class PipelineCache;
    class PipelineCodeFolder;
    class ZoneCounters;
    struct PipelineCacheSpec;

    /// Whether a pipeline is made to say what the driver's compiler made of it — registers, spills,
    /// shared memory — which only the harness's reports and the spill gate read. Asked for at
    /// creation, since it cannot be asked for afterwards, and paid in every compile it is asked of.
    enum class PipelineStatistics : std::uint8_t
    {
        Captured,
        Skipped,
    };

    /// What a device is made to say of the work on it: every pipeline of itself
    /// (`Device::reportPipeline`), and every zone of a frame what its counters read.
    struct DeviceReport
    {
        PipelineStatistics mStatistics = PipelineStatistics::Skipped;

        /// Where the driver's own code of each pipeline is written as text, a file a pipeline
        /// (`PipelineCodeFolder`), or empty for none: the instructions a profiler's view shows, for
        /// a device no profiler there reads. Asked for at creation, as the statistics are, and
        /// RADV then compiles every pipeline past its cache.
        std::filesystem::path mCodeFolder{};

        /// Whether the zones read the device's counters (`Device::getZoneCounters`), where the
        /// driver offers them: `RunProfile::mZoneCounters`.
        bool mCounters = false;
    };

    /// Which `VkObjectType` a handle is, so that `Device::setName` takes the handle and never a
    /// type a caller could get wrong. Every handle this backend names is listed; one that is not is
    /// a compile error rather than a name filed under the wrong kind. The handles are distinct
    /// pointer types on the 64-bit targets this backend runs on, which is what lets them be told
    /// apart at all.
    template <class Handle>
    struct ObjectTypeOf;

    template <>
    struct ObjectTypeOf<VkBuffer>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_BUFFER;
    };

    template <>
    struct ObjectTypeOf<VkImage>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_IMAGE;
    };

    template <>
    struct ObjectTypeOf<VkImageView>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_IMAGE_VIEW;
    };

    template <>
    struct ObjectTypeOf<VkSampler>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_SAMPLER;
    };

    template <>
    struct ObjectTypeOf<VkPipeline>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_PIPELINE;
    };

    template <>
    struct ObjectTypeOf<VkSemaphore>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_SEMAPHORE;
    };

    template <>
    struct ObjectTypeOf<VkQueryPool>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_QUERY_POOL;
    };

    template <>
    struct ObjectTypeOf<VkAccelerationStructureKHR>
    {
        static constexpr VkObjectType value = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
    };

    /// What a checkpoint on the queue points at: the zone the timer opened and the frame it was
    /// opened for, so a device loss can say "frame 83, `tlas`". Owned by the timer that set it
    /// and stable while the timer's slot lives, which is longer than a fault takes to be reported.
    struct Checkpoint
    {
        std::string_view mName;
        std::uint64_t mFrame = 0;
    };

    /// A logical device, its single queue, and the extension entry points.
    class Device
    {
    public:
        /// Takes the swapchain where `instance` loaded a surface, which it does for a window and
        /// never headless, and each option (`getOptionalExtensions`) the device offers whole whose
        /// needs are met by what is taken by then.
        ///
        /// @param instance must outlive the device. Not held: a `VkDevice` does not reference its
        ///        instance, but every entry point reached through it does.
        /// @param shaders the compiled shaders every pipeline on this device is built from, and what
        ///        the pipeline cache is keyed on: one directory for both, so no pipeline loads a
        ///        module the cache was not keyed on. Where they count, the device holds the census
        ///        they count into (`getCensus`).
        /// @param cache where the pipeline cache is kept. An empty directory keeps none, and every
        ///        pipeline is compiled from source every run.
        /// @param report what the pipelines are made to say of themselves (`reportPipeline`).
        Device(const Instance& instance, PhysicalDevice&& physicalDevice, const ShaderSet& shaders,
            const PipelineCacheSpec& cache, const DeviceReport& report);
        ~Device();

        VkDevice getHandle() const { return mHandle.get(); }
        VkQueue getQueue() const { return mQueue; }
        std::uint32_t getQueueFamily() const { return mPhysicalDevice.getQueueFamily(); }
        const PhysicalDevice& getPhysicalDevice() const { return mPhysicalDevice; }

        /// Where every buffer's and every image's memory comes from: one suballocator for the
        /// device, where an allocation per resource is a kernel call apiece for a cell's several
        /// hundred images. Not const although the device is, because every resource that asks holds
        /// the device by const reference.
        MemoryAllocator& getMemory() const;

        /// The queue's clock, which every submit signals and every wait reads.
        Timeline& getTimeline() const { return *mTimeline; }

        /// The queue's one command pool, which every submit is made out of, and what the queue may
        /// still be reading, which everything on the frame path lets go of through. The queue's
        /// and not the renderer's, so whatever holds the device can submit and bury without being
        /// handed either: a pool and a graveyard threaded through every constructor was two
        /// references beside a third they could have been reached through.
        CommandPool& getPool() const { return *mPool; }
        Graveyard& getGraveyard() const { return *mGraveyard; }

        /// Whether `vkQueuePresentKHR` may be handed a fence it signals when the presentation
        /// engine has finished with an image — the only thing that says so, since a queue-idle
        /// proves the queue is empty and not that the compositor has let go.
        bool hasPresentFences() const { return mPresentFences; }

        /// Whether a present carries an id and a wait for it can be asked (`Swapchain::awaitLatency`).
        bool hasPresentWait() const { return mPresentWait; }

        /// Handed to every `vkCreate*Pipelines` on this device, so that a shader is compiled once
        /// per change rather than once per pipeline.
        VkPipelineCache getPipelineCache() const;

        /// Writes the pipeline cache's file where it changed since it was last read or written —
        /// `PipelineCache::save`.
        void savePipelineCache();

        const std::filesystem::path& getShaderDirectory() const { return mShaderDirectory; }

        /// The census every pipeline made on the device counts its stores that were not finite
        /// into, or null where its shaders do not count. The device's and not the renderer's, for
        /// the reason the pool is: every layout binds it and every push writes it, which reach it
        /// where a pipeline is made and pushed rather than having it handed to every pass.
        const NotFiniteCensus* getCensus() const { return mCensus.get(); }

        /// The counters every zone of a frame reads (`GpuTimer`), or null where the run did not ask
        /// for them or the driver offers none. The device's, because the profiling lock they hold is.
        const ZoneCounters* getZoneCounters() const { return mZoneCounters.get(); }

        /// Whether pipelines are made to say what they are made of (`DeviceReport`), the flags
        /// every create info starts from.
        VkPipelineCreateFlags getPipelineFlags() const
        {
            VkPipelineCreateFlags flags = 0;
            if (mReport.mStatistics == PipelineStatistics::Captured)
                flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
            if (!mReport.mCodeFolder.empty())
                flags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
            return flags;
        }

        /// Logs what the driver's compiler made of `pipeline` — registers a thread, spills, shared
        /// memory a block — where the device captures it, and its compile time. The register count
        /// exists only inside the driver, which is what lets an occupancy figure be had here with no
        /// external profiler. Where the device is asked for its code, writes it (`writeCode`).
        void reportPipeline(
            VkPipeline pipeline, std::string_view name, std::optional<double> compileMs = std::nullopt) const;

        /// What the driver's compiler made `pipeline` use, the most of any of its executables, where
        /// the driver says: nothing of a figure no statistic of its name reports.
        ///
        /// @param point which kind of pipeline it is: a ray tracing one is a launch of this
        ///        renderer's, whose ray generation group is its first (`makeTracePipeline`).
        CompiledUse compiledUseOf(VkPipeline pipeline, VkPipelineBindPoint point) const;

        /// Attaches a name to a Vulkan object so captures and validation messages name it. Compiled
        /// to nothing in release: an unreadable capture is a debugging session that does not
        /// happen, and a released build has no captures.
        template <class Handle>
        void setName([[maybe_unused]] Handle handle, [[maybe_unused]] std::string_view name) const
        {
            // Terminated here and nowhere else, so a release build constructs nothing at all — and a
            // caller may hand over a literal or a view into a path it is already holding.
            if constexpr (sDebugNames)
                setNameImpl(
                    ObjectTypeOf<Handle>::value, reinterpret_cast<std::uint64_t>(handle), std::string(name).c_str());
        }

        /// Whether `setName` reaches the driver, which is what says a name may be chained into a
        /// create info that has no object of its own to name.
        bool namesObjects() const { return sDebugNames && mSetObjectName != nullptr; }

        /// Whether every submit is logged and watched to its end, `ValidationOptions::mWatchSubmits`.
        bool watchesSubmits() const { return mWatchesSubmits; }

        /// Opens a named region in `commands`, so a capture or a profile shows what each stretch of
        /// the frame is. Kept in release, which is the build a profiler reads; `name` arrives
        /// terminated, so a label costs a call and no allocation.
        void beginLabel(VkCommandBuffer commands, const char* name) const
        {
            if (mBeginLabel != nullptr)
                beginLabelImpl(commands, name);
        }

        void endLabel(VkCommandBuffer commands) const
        {
            if (mEndLabel != nullptr)
                mEndLabel(commands);
        }

        /// Marks the queue's progress with `checkpoint`, which the queue reports as the last one
        /// each stage passed if the device is lost: NVIDIA's checkpoints, or AMD's buffer markers,
        /// whichever the driver offers. Compiled to nothing in release, and nothing where the
        /// driver offers neither.
        void checkpoint(VkCommandBuffer commands, const Checkpoint* checkpoint) const
        {
            if constexpr (sDebugNames)
            {
                if (mCmdSetCheckpoint != nullptr)
                    mCmdSetCheckpoint(commands, checkpoint);
                if (mMarkers != nullptr)
                    writeMarker(commands, checkpoint);
            }
        }

        /// Blocks until the queue has signalled `value` on the timeline, and then lets go of what
        /// the queue can no longer be reading: the graveyard's burials and the pool's finished
        /// command buffers. The one way to wait, so that no wait is made without the collect a
        /// wait owes. `what` names the wait in the error a device that stops answering produces.
        void waitFor(std::uint64_t value, const char* what) const;

        /// Blocks until the queue has finished everything, tells the clock so, and collects as
        /// `waitFor` does. For tearing down and for resizing, not for pacing a frame.
        void waitIdle() const;

        /// Lets go of everything the graveyard and the pool hold, a burial stamped for a submit
        /// nobody has made included. For a queue nothing is on and nothing is recorded for — a
        /// drain and a teardown — which both assert.
        void collectIdle() const;

        /// What the device says about why it was lost, as lines for the message that reports it.
        /// Nothing where the driver offers no `VK_EXT_device_fault`. After a loss and never before,
        /// because the extension forbids the question of a device that is still answering.
        std::string describeFault() const;

        /// The last checkpoint each stage of the queue passed by AMD's buffer markers, as lines for
        /// the report of a wait that ran out: words in host memory, which a device still answering
        /// can be asked, where NVIDIA's checkpoints are asked only of a lost one
        /// (`VUID-vkGetQueueCheckpointDataNV-queue-02025`). Nothing where the driver offers no
        /// markers.
        std::string describeMarkers() const;

        // Read by the tests and by nothing else.
        /// Whether the driver offers `VK_EXT_device_fault` with its feature, and so whether
        /// `describeFault` has anything to ask.
        bool canDescribeFault() const { return mGetDeviceFaultInfo != nullptr; }

    private:
        /// What every wait ends with: the graveyard's burials and the pool's finished command
        /// buffers, let go of as far as the clock now reaches.
        void collect() const;

        /// How many executables the driver made `pipeline` of, and what it says of one: the
        /// statistics a pipeline was made to capture (`getPipelineFlags`).
        std::uint32_t executablesOf(VkPipeline pipeline) const;
        std::vector<VkPipelineExecutableStatisticKHR> statisticsOf(VkPipeline pipeline, std::uint32_t executable) const;

        /// Writes the text forms the driver gives the executable `executable` of `pipeline`'s
        /// `executables` in, the `copy`th of its name (`PipelineCodeFolder::claim`), to `mCode`.
        /// Nothing where the driver gives none, as NVIDIA's does.
        void writeCode(VkPipeline pipeline, std::string_view name, std::uint32_t copy, std::uint32_t executable,
            std::uint32_t executables) const;

        /// The last checkpoint each stage of the queue passed, as lines for the fault report.
        /// Nothing where the driver offers neither checkpoints nor buffer markers.
        std::string describeCheckpoints() const;

        /// `BufferMarkers::mark`, out of line, where the markers' type is whole.
        void writeMarker(VkCommandBuffer commands, const Checkpoint* checkpoint) const;

        void setNameImpl(VkObjectType type, std::uint64_t handle, const char* name) const;
        void beginLabelImpl(VkCommandBuffer commands, const char* name) const;

        PhysicalDevice mPhysicalDevice;

        /// Before every member made on it — `Root` says why.
        Root<VkDevice, vkDestroyDevice> mHandle;
        VkQueue mQueue = VK_NULL_HANDLE;
        PFN_vkSetDebugUtilsObjectNameEXT mSetObjectName = nullptr;
        PFN_vkCmdBeginDebugUtilsLabelEXT mBeginLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT mEndLabel = nullptr;

        /// Null where the driver offers no `VK_EXT_device_fault`, or offers the extension without
        /// its feature.
        PFN_vkGetDeviceFaultInfoEXT mGetDeviceFaultInfo = nullptr;

        /// Null where the driver offers no `VK_NV_device_diagnostic_checkpoints`.
        PFN_vkCmdSetCheckpointNV mCmdSetCheckpoint = nullptr;
        PFN_vkGetQueueCheckpointDataNV mGetQueueCheckpointData = nullptr;

        bool mPresentFences = false;
        bool mPresentWait = false;
        bool mWatchesSubmits = false;

        std::filesystem::path mShaderDirectory;
        DeviceReport mReport;

        /// Where the pipelines' code goes, null where `mReport` asks for none.
        std::unique_ptr<PipelineCodeFolder> mCode;

        // Last, so that they are torn down first, and in this order, because a later one dies
        // earlier: everything after the graveyard buries into it as it goes, the graveyard ends
        // what it holds against the clock and gives memory back to the allocator, and saving the
        // cache and freeing a block both call on the device that `mHandle` closes last of all.
        std::unique_ptr<PipelineCache> mPipelineCache;
        std::unique_ptr<MemoryAllocator> mMemory;
        std::unique_ptr<Timeline> mTimeline;
        std::unique_ptr<Graveyard> mGraveyard;
        std::unique_ptr<CommandPool> mPool;

        /// AMD's breadcrumbs, null where the driver offers no `VK_AMD_buffer_marker`. Made on the
        /// allocator, so torn down before it. Marked from `checkpoint`, which a recording calls on
        /// the one thread that records.
        std::unique_ptr<BufferMarkers> mMarkers;

        /// Made on the allocator, so torn down before it.
        std::unique_ptr<NotFiniteCensus> mCensus;

        /// Holds the profiling lock, which goes before the device does.
        std::unique_ptr<ZoneCounters> mZoneCounters;
    };
}
