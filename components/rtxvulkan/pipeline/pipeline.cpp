#include "pipeline.hpp"

#include <optional>

#include <components/rtxvulkan/device/device.hpp>

namespace Rtx
{
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
