#include "pipelinereport.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <components/debug/debuglog.hpp>

#include "device.hpp"
#include "pipelinecode.hpp"
#include "pipelinetally.hpp"
#include "result.hpp"

namespace Rtx
{
    namespace
    {
        void logSummaryOf(const std::string_view what, const PipelineSummary& summary)
        {
            Log(Debug::Info) << what << ": " << describePipelines(summary);

            // **A spill is a warning**: a kernel that spills runs at the speed of its scratch memory, which
            // took a frame on an integrated card from milliseconds to the better part of a second
            // (`docs/research/2026-10-21-amd-integrated-stage-times.md`). One line for them all, the worst
            // first.
            if (const std::string spills = describeSpills(summary); !spills.empty())
                Log(Debug::Warning) << what << ": " << spills;
        }

        void raise(std::optional<std::uint64_t>& most, const std::uint64_t value)
        {
            most = std::max(most.value_or(0), value);
        }

        std::uint64_t unsignedOf(const VkPipelineExecutableStatisticKHR& statistic)
        {
            switch (statistic.format)
            {
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                    return statistic.value.i64 > 0 ? static_cast<std::uint64_t>(statistic.value.i64) : 0;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                    return statistic.value.u64;
                default:
                    return 0;
            }
        }

        /// Raises what `use` says by one statistic the driver reported under one of `names`.
        void readUse(CompiledUse& use, const VkPipelineExecutableStatisticKHR& statistic, const StatisticNames& names)
        {
            const std::string_view name = statistic.name;
            const std::uint64_t value = unsignedOf(statistic);
            if (name == names.mScratch)
                raise(use.mScratchBytes, value & names.mScratchBits);
            else if (name == names.mRegisters)
                raise(use.mRegisters, value);
            else if (name == names.mShared)
                raise(use.mSharedBytes, value);
        }
    }

    std::string describeStatistics(
        const std::span<const VkPipelineExecutableStatisticKHR> statistics, const StatisticNames& names)
    {
        // Whatever the driver chose to say, and not a list this side picked: the names are the compiler's own,
        // so a fixed set of fields would go empty on the next driver.
        std::string line;
        for (const VkPipelineExecutableStatisticKHR& statistic : statistics)
        {
            if (!line.empty())
                line += ", ";
            line += statistic.name;
            line += ' ';
            if (std::string_view(statistic.name) == names.mScratch)
            {
                line += std::to_string(unsignedOf(statistic) & names.mScratchBits);
                continue;
            }
            switch (statistic.format)
            {
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                    line += statistic.value.b32 != VK_FALSE ? "yes" : "no";
                    break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                    line += std::to_string(statistic.value.i64);
                    break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                    line += std::to_string(statistic.value.u64);
                    break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                    line += std::to_string(statistic.value.f64);
                    break;
                default:
                    line += '?';
                    break;
            }
        }
        return line;
    }

    PipelineReport::PipelineReport(const Device& device, const std::filesystem::path& codeFolder)
        : mDevice(device)
        , mCode(codeFolder.empty() ? nullptr : std::make_unique<PipelineCodeFolder>(codeFolder))
        , mTally(std::make_unique<PipelineTally>(std::chrono::steady_clock::now()))
    {
    }

    PipelineReport::~PipelineReport()
    {
        if (const PipelineSummary rest = mTally->take(std::chrono::steady_clock::now()); rest.mMade > 0)
            logSummaryOf(mSummarised ? "Pipelines later in the session" : "Pipelines of the session", rest);
    }

    VkPipelineCreateFlags PipelineReport::getFlags() const
    {
        VkPipelineCreateFlags flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
        if (mCode != nullptr)
            flags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
        return flags;
    }

    std::vector<VkPipelineExecutablePropertiesKHR> PipelineReport::executablePropertiesOf(
        const VkPipeline pipeline) const
    {
        const VkPipelineInfoKHR asked{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
        };
        return enumerateVk<VkPipelineExecutablePropertiesKHR>(
            mDevice, "vkGetPipelineExecutablePropertiesKHR",
            [&](std::uint32_t* count, VkPipelineExecutablePropertiesKHR* into) {
                return vkGetPipelineExecutablePropertiesKHR(mDevice.getHandle(), &asked, count, into);
            },
            VkPipelineExecutablePropertiesKHR{ .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR,
                .pNext = nullptr,
                .stages = 0,
                .name = {},
                .description = {},
                .subgroupSize = 0 });
    }

    std::vector<VkPipelineExecutableStatisticKHR> PipelineReport::statisticsOf(
        const VkPipeline pipeline, const std::uint32_t executable) const
    {
        const VkPipelineExecutableInfoKHR which{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
            .executableIndex = executable,
        };

        return enumerateVk<VkPipelineExecutableStatisticKHR>(
            mDevice, "vkGetPipelineExecutableStatisticsKHR",
            [&](std::uint32_t* count, VkPipelineExecutableStatisticKHR* into) {
                return vkGetPipelineExecutableStatisticsKHR(mDevice.getHandle(), &which, count, into);
            },
            VkPipelineExecutableStatisticKHR{ .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR,
                .pNext = nullptr,
                .name = {},
                .description = {},
                .format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR,
                .value = {} });
    }

    CompiledUse PipelineReport::compiledUseOf(const VkPipeline pipeline, const VkPipelineBindPoint point) const
    {
        CompiledUse use;
        if (point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            use.mStackBytes = vkGetRayTracingShaderGroupStackSizeKHR(
                mDevice.getHandle(), pipeline, 0, VK_SHADER_GROUP_SHADER_GENERAL_KHR);

        const std::vector<VkPipelineExecutablePropertiesKHR> executables = executablePropertiesOf(pipeline);
        const StatisticNames& names = mDevice.getPhysicalDevice().getDriver().mStatistics;
        for (std::uint32_t executable = 0; executable < executables.size(); ++executable)
        {
            use.mSubgroupSize = std::max(use.mSubgroupSize.value_or(0), executables[executable].subgroupSize);
            for (const VkPipelineExecutableStatisticKHR& statistic : statisticsOf(pipeline, executable))
            {
                readUse(use, statistic, names);
            }
        }
        return use;
    }

    void PipelineReport::report(
        const VkPipeline pipeline, const std::string_view name, const PipelineFeedback& feedback) const
    {
        // One line a pipeline, at the debug level and built only where it is shown: a start makes hundreds,
        // which `logSummary` says in a line.
        const bool listed = Log::sMinDebugLevel >= Debug::Debug;
        std::string line;
        if (listed)
        {
            switch (feedback.mSource)
            {
                case PipelineSource::Compiled:
                    line = std::format("pipeline {}: compiled in {:.2f} ms", name, feedback.mDurationMs);
                    break;
                case PipelineSource::Cached:
                    line = std::format(
                        "pipeline {}: found in the pipeline cache in {:.2f} ms", name, feedback.mDurationMs);
                    break;
                case PipelineSource::Unsaid:
                    line = std::format("pipeline {}: made", name);
                    break;
            }
        }

        const std::vector<VkPipelineExecutablePropertiesKHR> executables = executablePropertiesOf(pipeline);
        const auto count = static_cast<std::uint32_t>(executables.size());
        const std::uint32_t copy = mCode != nullptr ? mCode->claim(name) : 0;

        // Said rather than left as a missing part of the line: NVIDIA's compiler reports no executable for a
        // ray tracing pipeline and no internal representation for any, so a reader could not otherwise tell it
        // from a call nobody made. Nsight Graphics is where that is read.
        if (count == 0 && listed)
            line += "; the driver reports no executable";

        const StatisticNames& names = mDevice.getPhysicalDevice().getDriver().mStatistics;
        CompiledUse use;
        for (std::uint32_t executable = 0; executable < count; ++executable)
        {
            if (mCode != nullptr)
                writeCode(pipeline, name, copy, executable, count);

            const std::vector<VkPipelineExecutableStatisticKHR> statistics = statisticsOf(pipeline, executable);
            for (const VkPipelineExecutableStatisticKHR& statistic : statistics)
                readUse(use, statistic, names);
            if (listed)
                line += std::format("; {}{}{}", count > 1 ? executables[executable].name : "", count > 1 ? ": " : "",
                    describeStatistics(statistics, names));
        }

        if (listed)
            Log(Debug::Debug) << line;
        mTally->add(PipelineMade{ .mName = name,
                        .mFeedback = feedback,
                        .mExecutables = count,
                        .mScratchBytes = use.mScratchBytes.value_or(0) },
            std::chrono::steady_clock::now());
    }

    void PipelineReport::logSummary(const std::string_view what)
    {
        logSummaryOf(what, mTally->take(std::chrono::steady_clock::now()));
        mSummarised = true;
    }

    void PipelineReport::writeCode(const VkPipeline pipeline, const std::string_view name, const std::uint32_t copy,
        const std::uint32_t executable, const std::uint32_t executables) const
    {
        const VkPipelineExecutableInfoKHR which{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
            .pNext = nullptr,
            .pipeline = pipeline,
            .executableIndex = executable,
        };
        const auto represent = [&](std::uint32_t* count, VkPipelineExecutableInternalRepresentationKHR* into) {
            return vkGetPipelineExecutableInternalRepresentationsKHR(mDevice.getHandle(), &which, count, into);
        };

        // Asked twice: once for the forms and the size of each, and again with room for the bodies of
        // the text forms alone, which are all that is written. **A form not asked for keeps a null
        // `pData`**, which the second call answers with its size and no body: AMD's Windows driver
        // answers `VK_INCOMPLETE` for a ray tracing launch's two binary forms whatever room they are
        // given (`docs/research/2026-11-13-amd-pipeline-code-forms.md`).
        std::vector<VkPipelineExecutableInternalRepresentationKHR> forms
            = enumerateVk<VkPipelineExecutableInternalRepresentationKHR>(mDevice,
                "vkGetPipelineExecutableInternalRepresentationsKHR", represent,
                VkPipelineExecutableInternalRepresentationKHR{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR,
                    .pNext = nullptr,
                    .name = {},
                    .description = {},
                    .isText = VK_FALSE,
                    .dataSize = 0,
                    .pData = nullptr });
        std::vector<std::string> bodies(forms.size());
        for (std::size_t form = 0; form < forms.size(); ++form)
        {
            if (forms[form].isText == VK_FALSE)
                continue;
            bodies[form].resize(forms[form].dataSize);
            forms[form].pData = bodies[form].data();
        }
        auto count = static_cast<std::uint32_t>(forms.size());

        // Still incomplete with the room the driver said its text needs is the driver's fault, and the
        // code a diagnostic: left out with a warning, so the run that asked for it goes on.
        const VkResult filled = represent(&count, forms.data());
        if (filled == VK_INCOMPLETE)
        {
            Log(Debug::Warning) << "pipeline " << name << ": its driver answered VK_INCOMPLETE for its code with the "
                                << "room it said the code needs, so its code is left out";
            return;
        }
        checkVk(mDevice, filled, "vkGetPipelineExecutableInternalRepresentationsKHR");

        std::vector<PipelineCodeForm> text;
        for (std::size_t form = 0; form < forms.size(); ++form)
        {
            if (forms[form].isText == VK_FALSE)
                continue;

            // A text form's size counts its terminator.
            std::string_view body(bodies[form].data(), forms[form].dataSize);
            while (!body.empty() && body.back() == '\0')
                body.remove_suffix(1);
            text.push_back(
                PipelineCodeForm{ .mName = forms[form].name, .mDescription = forms[form].description, .mText = body });
        }
        if (!text.empty())
            mCode->write(name, copy, executable, executables, text);
    }
}
