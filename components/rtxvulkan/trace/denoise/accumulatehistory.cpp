#include "accumulatehistory.hpp"

#include <cassert>
#include <cstddef>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/accumulate.h>
#include <components/rtx/shaders/atrous.h>
#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/device/memory/formats.hpp>

namespace Rtx
{
    namespace
    {
        /// `SAMPLED` beside `STORAGE` on what the cascade after the accumulator reads. `AtrousPass`
        /// takes its taps through the texture unit and a sampled descriptor needs the bit at
        /// creation, which is a promise made here and kept there.
        constexpr VkImageUsageFlags sReadAndWrite = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    float AccumulateHistory::distanceScaleFor(const float far)
    {
        assert(far > 0.0f && "a frame with no far plane to scale a stored distance by");
        return Shaders::ACCUMULATE_DISTANCE_RANGE / far;
    }

    AccumulateHistory::AccumulateHistory(const Device& device)
        : mDevice(device)
    {
    }

    void AccumulateHistory::resize(std::uint32_t width, std::uint32_t height)
    {
        if (!mBlended.isEmpty() && mBlended.getWidth() == width && mBlended.getHeight() == height)
            return;

        for (std::size_t i = 0; i < 2; ++i)
        {
            mColour[i] = Image(mDevice, width, height, toVulkanFormat(ACCUMULATE_COLOUR), sReadAndWrite,
                i == 0 ? "accumulate-colour-0" : "accumulate-colour-1");
            mSurface[i] = Image(mDevice, width, height, toVulkanFormat(ACCUMULATE_SURFACE), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "accumulate-surface-0" : "accumulate-surface-1");
            mMoments[i] = Image(mDevice, width, height, toVulkanFormat(ACCUMULATE_MOMENTS), sReadAndWrite,
                i == 0 ? "accumulate-moments-0" : "accumulate-moments-1");
        }

        mBlended = Image(mDevice, width, height, toVulkanFormat(ATROUS_CHANNEL), sReadAndWrite, "accumulate-blended");

        mTurns.restart();
    }

    AccumulateHistory::Turn AccumulateHistory::turn()
    {
        assert(!mBlended.isEmpty() && "a turn before resize");

        const HistoryTurns::Step step = mTurns.next();
        return Turn{
            .mColourBefore = mColour[step.mBefore],
            .mSurfaceBefore = mSurface[step.mBefore],
            .mMomentsBefore = mMoments[step.mBefore],
            .mColour = mColour[step.mNow],
            .mSurface = mSurface[step.mNow],
            .mMoments = mMoments[step.mNow],
            .mBlended = mBlended,
            .mFresh = step.mFresh,
        };
    }

    const Image& AccumulateHistory::getBlended() const
    {
        assert(!mBlended.isEmpty() && "the blend asked for before a resize made one");
        assert(!mTurns.isFresh() && "the blend asked for before any frame was accumulated into it");

        return mBlended;
    }

    const Image& AccumulateHistory::getHistory() const
    {
        assert(!mColour[mTurns.getNow()].isEmpty() && "the history asked for before a resize made one");
        assert(!mTurns.isFresh() && "the history asked for before the frame that hands it over");

        return mColour[mTurns.getNow()];
    }
}
