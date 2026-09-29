#include "shadowhistory.hpp"

#include <cassert>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/shadow.h>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    ShadowHistory::ShadowHistory(const Device& device)
        : mDevice(device)
    {
    }

    void ShadowHistory::resize(std::uint32_t width, std::uint32_t height)
    {
        if (!mVisibility.isEmpty() && mVisibility.getWidth() == width && mVisibility.getHeight() == height)
            return;

        constexpr VkFormat reprojected = toVulkanFormat(SHADOW_REPROJECTED);
        for (std::size_t i = 0; i < 2; ++i)
            mMoments[i] = Image(mDevice, width, height, toVulkanFormat(SHADOW_MOMENTS), VK_IMAGE_USAGE_STORAGE_BIT,
                i == 0 ? "shadow-moments-0" : "shadow-moments-1");

        mHistory = Image(mDevice, width, height, reprojected, VK_IMAGE_USAGE_STORAGE_BIT, "shadow-history");
        mScratch = Image(mDevice, width, height, reprojected, VK_IMAGE_USAGE_STORAGE_BIT, "shadow-scratch");
        mVisibility = Image(mDevice, width, height, reprojected, VK_IMAGE_USAGE_STORAGE_BIT, "shadow-visibility");
        mTiles
            = Image(mDevice, groupsFor(width, Shaders::SHADOW_WORKGROUP), groupsFor(height, Shaders::SHADOW_WORKGROUP),
                toVulkanFormat(SHADOW_TILES), VK_IMAGE_USAGE_STORAGE_BIT, "shadow-tiles");
        mMask = Image(mDevice, groupsFor(width, Shaders::SHADOW_MASK_WIDTH),
            groupsFor(height, Shaders::SHADOW_MASK_HEIGHT), toVulkanFormat(SHADOW_MASK), VK_IMAGE_USAGE_STORAGE_BIT,
            "shadow-mask");

        mCurrent = 0;
        mFresh = true;
    }

    ShadowHistory::Turn ShadowHistory::turn()
    {
        assert(!mVisibility.isEmpty() && "a turn before resize");

        const std::size_t previous = mCurrent;
        mCurrent = 1 - mCurrent;

        const bool fresh = mFresh;
        mFresh = false;

        return Turn{
            .mMomentsBefore = mMoments[previous],
            .mMoments = mMoments[mCurrent],
            .mHistory = mHistory,
            .mScratch = mScratch,
            .mVisibility = mVisibility,
            .mTiles = mTiles,
            .mMask = mMask,
            .mFresh = fresh,
        };
    }
}
