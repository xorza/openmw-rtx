#include "specularhistory.hpp"

#include <cassert>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/specular.h>
#include <components/rtxvulkan/device/memory/formats.hpp>

namespace Rtx
{
    SpecularHistory::SpecularHistory(const Device& device)
        : mDevice(device)
    {
    }

    void SpecularHistory::resize(std::uint32_t width, std::uint32_t height)
    {
        if (!mMeans[0].isEmpty() && mMeans[0].getWidth() == width && mMeans[0].getHeight() == height)
            return;

        for (std::size_t i = 0; i < 2; ++i)
        {
            mMeans[i] = Image(mDevice, width, height, toVulkanFormat(SPECULAR_MEAN), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "specular-mean-0" : "specular-mean-1");
            mFrames[i] = Image(mDevice, width, height, toVulkanFormat(SPECULAR_FRAMES), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "specular-frames-0" : "specular-frames-1");
        }

        mCurrent = 0;
        mFresh = true;
    }

    SpecularHistory::Turn SpecularHistory::turn()
    {
        assert(!mMeans[0].isEmpty() && "a turn before resize");

        const std::size_t previous = mCurrent;
        mCurrent = 1 - mCurrent;

        const bool fresh = mFresh;
        mFresh = false;

        return Turn{
            .mMeanBefore = mMeans[previous],
            .mFramesBefore = mFrames[previous],
            .mMean = mMeans[mCurrent],
            .mFrames = mFrames[mCurrent],
            .mFresh = fresh,
        };
    }
}
