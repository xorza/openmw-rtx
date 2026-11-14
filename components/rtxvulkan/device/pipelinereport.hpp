#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include "driverprofile.hpp"

namespace Rtx
{
    class Device;
    class PipelineCodeFolder;
    class PipelineTally;
    struct PipelineFeedback;

    /// What a pipeline's compiler made it use: the statistics `VK_KHR_pipeline_executable_properties`
    /// reports under names each driver picks, read by the target drivers' own names — AMD's own
    /// driver's, RADV's and NVIDIA's — and a launch's stack, which every driver answers. A figure the
    /// driver does not give is nothing.
    struct CompiledUse
    {
        /// The scratch memory a lane uses, in bytes — what the compiler spilled out of the registers, and
        /// any array indexed at run time: AMD's `scratchMemUsageInBytes`, RADV's `Scratch size` and
        /// NVIDIA's `Local Memory Size` (`StatisticNames::mScratchBits`). RADV's `Spilled SGPRs` are not a
        /// spill to memory: ACO keeps a scalar register spilled in a vector register's lanes.
        std::optional<std::uint64_t> mScratchBytes;

        /// The vector registers a lane holds, which is how many waves share a SIMD's register file:
        /// AMD's `numUsedVgprs`, RADV's `VGPRs` and NVIDIA's `Register Count`.
        std::optional<std::uint64_t> mRegisters;

        /// The shared memory a workgroup holds, in bytes, what the kernel declares and what the
        /// compiler put there of its own, a ray query's traversal among it: AMD's
        /// `ldsUsageSizeInBytes`, RADV's `LDS size` and NVIDIA's `Shared Memory Size`.
        std::optional<std::uint64_t> mSharedBytes;

        /// A launch's ray generation stack, in bytes a lane, which every driver answers: where a
        /// launch keeps what does not fit its registers. **What a launch is measured by on AMD's
        /// own driver**, whose statistics for one describe a stub of three registers whatever the
        /// launch does. Nothing for a pipeline that is not a launch.
        std::optional<std::uint64_t> mStackBytes;

        /// The lanes a subgroup of the pipeline holds, the widest of its executables, which every
        /// driver that reports an executable answers: what a register count is a count of.
        std::optional<std::uint32_t> mSubgroupSize;
    };

    /// One executable's statistics as a pipeline's log line says them: each under the driver's own name, in
    /// the driver's order, the scratch figure masked to its bytes (`StatisticNames::mScratchBits`).
    std::string describeStatistics(
        std::span<const VkPipelineExecutableStatisticKHR> statistics, const StatisticNames& names);

    /// What the pipelines made on a device say of themselves: each one's making and what its compiler made of it,
    /// counted towards a summary, and its code where a run asks for it. The device's, and pinned, as every part
    /// the device makes is: it holds the device it asks.
    class PipelineReport
    {
    public:
        /// @param codeFolder where each pipeline's code is written (`DeviceReport::mCodeFolder`), or empty for
        ///        none.
        PipelineReport(const Device& device, const std::filesystem::path& codeFolder);

        /// Logs what came after the last summary. Every compile on the device has ended by then: what makes
        /// pipelines is torn down before the device.
        ~PipelineReport();

        PipelineReport(const PipelineReport&) = delete;
        PipelineReport& operator=(const PipelineReport&) = delete;
        PipelineReport(PipelineReport&&) = delete;
        PipelineReport& operator=(PipelineReport&&) = delete;

        /// The flags every create info starts from: their statistics always (`docs/architecture.md`, "Every
        /// pipeline captures its statistics"), their code where the run asks.
        VkPipelineCreateFlags getFlags() const;

        /// Counts `pipeline` towards the next `logSummary`, and logs at the debug level what its making took and
        /// what the driver's compiler made of it — registers a thread, spills, shared memory a block: an occupancy
        /// figure with no external profiler. Where the run asks for its code, writes it (`writeCode`). From any
        /// compile hand.
        void report(VkPipeline pipeline, std::string_view name, const PipelineFeedback& feedback) const;

        /// Logs what the pipelines made since the device was made, or since the last summary, came to
        /// (`describePipelines`), and a warning naming every one of them that uses scratch memory, under `what`.
        void logSummary(std::string_view what);

        /// What the driver's compiler made `pipeline` use, the most of any of its executables, where the driver
        /// says: nothing of a figure no statistic of its name reports.
        ///
        /// @param point which kind of pipeline it is: a ray tracing one is a launch of this renderer's, whose ray
        ///        generation group is its first (`makeTracePipeline`).
        CompiledUse compiledUseOf(VkPipeline pipeline, VkPipelineBindPoint point) const;

    private:
        /// The executables the driver made `pipeline` of, and what it says of one: the statistics a pipeline was
        /// made to capture (`getFlags`).
        std::vector<VkPipelineExecutablePropertiesKHR> executablePropertiesOf(VkPipeline pipeline) const;
        std::vector<VkPipelineExecutableStatisticKHR> statisticsOf(VkPipeline pipeline, std::uint32_t executable) const;

        /// Writes the text forms the driver gives the executable `executable` of `pipeline`'s `executables` in, the
        /// `copy`th of its name (`PipelineCodeFolder::claim`), to `mCode`. Nothing where the driver gives none, as
        /// NVIDIA's does.
        void writeCode(VkPipeline pipeline, std::string_view name, std::uint32_t copy, std::uint32_t executable,
            std::uint32_t executables) const;

        const Device& mDevice;

        /// Where the pipelines' code goes, null where the run asks for none.
        std::unique_ptr<PipelineCodeFolder> mCode;

        /// What `report` counted towards the next summary, from the device's making.
        std::unique_ptr<PipelineTally> mTally;
        bool mSummarised = false;
    };
}
