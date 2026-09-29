#include "panehistory.hpp"

#include <cassert>
#include <cstddef>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/accumulate.h>
#include <components/rtx/shaders/pane.h>
#include <components/rtxvulkan/device/memory/formats.hpp>

namespace Rtx
{
    PaneHistory::PaneHistory(const Device& device)
        : mDevice(device)
    {
    }

    void PaneHistory::resize(std::uint32_t width, std::uint32_t height)
    {
        if (!mMeans[0].isEmpty() && mMeans[0].getWidth() == width && mMeans[0].getHeight() == height)
            return;

        for (std::size_t i = 0; i < 2; ++i)
        {
            mMeans[i] = Image(mDevice, width, height, toVulkanFormat(PANE_MEAN), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "pane-mean-0" : "pane-mean-1");
            mFrames[i] = Image(mDevice, width, height, toVulkanFormat(PANE_FRAMES), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "pane-frames-0" : "pane-frames-1");
            mHeld[i] = Image(mDevice, width, height, toVulkanFormat(ACCUMULATE_SURFACE), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "pane-held-0" : "pane-held-1");
        }

        mTurns.restart();
    }

    PaneHistory::Turn PaneHistory::turn()
    {
        assert(!mMeans[0].isEmpty() && "a turn before resize");

        const HistoryTurns::Step step = mTurns.next();
        return Turn{
            .mMeanBefore = mMeans[step.mBefore],
            .mFramesBefore = mFrames[step.mBefore],
            .mHeldBefore = mHeld[step.mBefore],
            .mMean = mMeans[step.mNow],
            .mFrames = mFrames[step.mNow],
            .mHeld = mHeld[step.mNow],
            .mFresh = step.mFresh,
        };
    }
}
