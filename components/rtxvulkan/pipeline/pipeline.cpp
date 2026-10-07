#include "pipeline.hpp"

#include <cstddef>
#include <optional>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/notfinitecensus.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx
{
    Specialization::Specialization(
        const Device& device, const std::string_view module, const std::span<const std::uint32_t> words)
        : mWords(words.begin(), words.end())
    {
        const auto entry = [](const std::uint32_t id, const std::size_t word) {
            return VkSpecializationMapEntry{ id, static_cast<std::uint32_t>(word * sizeof(std::uint32_t)),
                sizeof(std::uint32_t) };
        };

        const NotFiniteCensus* const census = device.getCensus();
        mEntries.reserve(words.size() + (census != nullptr ? 1 : 0));
        for (std::uint32_t at = 0; at < words.size(); ++at)
            mEntries.push_back(entry(at, at));
        if (census != nullptr)
        {
            mEntries.push_back(entry(Shaders::SPEC_CENSUS_KERNEL, mWords.size()));
            mWords.push_back(census->kernelOf(module));
        }

        mInfo = VkSpecializationInfo{
            .mapEntryCount = static_cast<std::uint32_t>(mEntries.size()),
            .pMapEntries = mEntries.data(),
            .dataSize = mWords.size() * sizeof(std::uint32_t),
            .pData = mWords.data(),
        };
    }

    PipelineCreation::PipelineCreation(const Device& device, const std::string_view name)
        : mDevice(device)
        , mName(name)
        , mNoted("compiling the pipeline \"{}\"", name)
    {
    }

    const void* PipelineCreation::chain(const void* next)
    {
        mTimed = VkPipelineCreationFeedbackCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,
            .pNext = next,
            .pPipelineCreationFeedback = &mFeedback,
            .pipelineStageCreationFeedbackCount = 0,
            .pPipelineStageCreationFeedbacks = nullptr,
        };
        return &mTimed;
    }

    void PipelineCreation::finish(const VkPipeline pipeline) const
    {
        constexpr VkPipelineCreationFeedbackFlags valid = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
        constexpr VkPipelineCreationFeedbackFlags fromApplicationCache
            = VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
        std::optional<double> compileMs;
        if ((mFeedback.flags & (valid | fromApplicationCache)) == valid)
            compileMs = static_cast<double>(mFeedback.duration) / 1e6;

        mDevice.setName(pipeline, mName);
        mDevice.reportPipeline(pipeline, mName, compileMs);
    }
}
