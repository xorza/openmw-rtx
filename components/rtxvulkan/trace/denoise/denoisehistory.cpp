#include "denoisehistory.hpp"

#include <cassert>
#include <initializer_list>
#include <string>

#include <components/rtx/shaders/accumulate.h>
#include <components/rtx/shaders/atrous.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/pane.h>
#include <components/rtx/shaders/shadow.h>
#include <components/rtx/shaders/specular.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        constexpr VkImageUsageFlags sStorage = VK_IMAGE_USAGE_STORAGE_BIT;

        /// `SAMPLED` beside `STORAGE` on what the cascade reads. `AtrousPass` takes its taps
        /// through the texture unit, and a sampled descriptor needs the bit at creation.
        constexpr VkImageUsageFlags sReadAndWrite = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    ImagePair ImagePair::make(const Device& device, const std::uint32_t width, const std::uint32_t height,
        const VkFormat format, const VkImageUsageFlags usage, const std::string_view name)
    {
        const std::string first = std::string(name) + "-0";
        const std::string second = std::string(name) + "-1";
        return ImagePair{ { Image(device, width, height, format, usage, first),
            Image(device, width, height, format, usage, second) } };
    }

    DenoiseHistory::DenoiseHistory(const Device& device)
        : mDevice(device)
    {
    }

    float DenoiseHistory::distanceScaleFor(const float far)
    {
        assert(far > 0.0f && "a frame with no far plane to scale a stored distance by");
        return Shaders::ACCUMULATE_DISTANCE_RANGE / far;
    }

    void DenoiseHistory::resize(const std::uint32_t width, const std::uint32_t height)
    {
        if (!mBlended.isEmpty() && mBlended.getWidth() == width && mBlended.getHeight() == height)
            return;

        mColour = ImagePair::make(
            mDevice, width, height, toVulkanFormat(ACCUMULATE_COLOUR), sReadAndWrite, "accumulate-colour");
        mSurface = ImagePair::make(
            mDevice, width, height, toVulkanFormat(ACCUMULATE_SURFACE), sStorage, "accumulate-surface");
        mMoments = ImagePair::make(
            mDevice, width, height, toVulkanFormat(ACCUMULATE_MOMENTS), sReadAndWrite, "accumulate-moments");
        mBlended = Image(mDevice, width, height, toVulkanFormat(ATROUS_CHANNEL), sReadAndWrite, "accumulate-blended");
        mScratch = Image(mDevice, width, height, toVulkanFormat(ATROUS_CHANNEL), sReadAndWrite, "atrous-scratch");

        constexpr VkFormat reprojected = toVulkanFormat(SHADOW_REPROJECTED);
        mShadowMoments
            = ImagePair::make(mDevice, width, height, toVulkanFormat(SHADOW_MOMENTS), sStorage, "shadow-moments");
        mShadowHistory = Image(mDevice, width, height, reprojected, sStorage, "shadow-history");
        mShadowScratch = Image(mDevice, width, height, reprojected, sStorage, "shadow-scratch");
        mShadowVisibility = Image(mDevice, width, height, reprojected, sStorage, "shadow-visibility");
        mShadowTiles = Image(mDevice, groupsFor(width, Shaders::SHADOW_WORKGROUP),
            groupsFor(height, Shaders::SHADOW_WORKGROUP), toVulkanFormat(SHADOW_TILES), sStorage, "shadow-tiles");
        mShadowMask = Image(mDevice, groupsFor(width, Shaders::SHADOW_MASK_WIDTH),
            groupsFor(height, Shaders::SHADOW_MASK_HEIGHT), toVulkanFormat(SHADOW_MASK), sStorage, "shadow-mask");

        mSpecularMeans
            = ImagePair::make(mDevice, width, height, toVulkanFormat(SPECULAR_MEAN), sStorage, "specular-mean");

        mPaneMeans = ImagePair::make(mDevice, width, height, toVulkanFormat(PANE_MEAN), sStorage, "pane-mean");
        mPaneHeld = ImagePair::make(mDevice, width, height, toVulkanFormat(ACCUMULATE_SURFACE), sStorage, "pane-held");

        mTurns = TemporalTurns{};
    }

    TemporalTurns::Step DenoiseHistory::turn(const TemporalFlags& runs)
    {
        assert(!mBlended.isEmpty() && "a turn before resize");
        return mTurns.next(runs);
    }

    void DenoiseHistory::discard(const VkCommandBuffer commands, const TemporalTurns::Step& step) const
    {
        Barriers barriers(commands);
        const auto discardFor = [&](const Temporal filter, std::initializer_list<const Image*> written,
                                    std::initializer_list<const Image*> read) {
            if (!step.mRuns[filter])
                return;

            if (step.mFresh[filter])
                for (const Image* image : read)
                    barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeRead));

            for (const Image* image : written)
                barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeWrite));
        };

        const AccumulateImages accumulated = accumulate(step);
        discardFor(Temporal::Accumulate,
            { &accumulated.mColour, &accumulated.mSurface, &accumulated.mMoments, &accumulated.mBlended,
                &accumulated.mScratch },
            { &accumulated.mColourBefore, &accumulated.mSurfaceBefore, &accumulated.mMomentsBefore });

        // The history the temporal pass reads is the first level's answer from the frame before,
        // so it is one of what a fresh history discards and not one of what the frame writes whole.
        const ShadowImages shadowed = shadow(step);
        discardFor(Temporal::Shadow,
            { &shadowed.mScratch, &shadowed.mMoments, &shadowed.mVisibility, &shadowed.mTiles, &shadowed.mMask },
            { &shadowed.mHistory, &shadowed.mMomentsBefore });

        const SpecularImages glossy = specular(step);
        discardFor(Temporal::Specular, { &glossy.mMean }, { &glossy.mMeanBefore });

        const PaneImages panes = pane(step);
        discardFor(Temporal::Pane, { &panes.mMean, &panes.mHeld }, { &panes.mMeanBefore, &panes.mHeldBefore });

        barriers.flush();
    }

    DenoiseHistory::AccumulateImages DenoiseHistory::accumulate(const TemporalTurns::Step& step) const
    {
        return AccumulateImages{
            .mColourBefore = mColour.before(step),
            .mSurfaceBefore = mSurface.before(step),
            .mMomentsBefore = mMoments.before(step),
            .mColour = mColour.now(step),
            .mSurface = mSurface.now(step),
            .mMoments = mMoments.now(step),
            .mBlended = mBlended,
            .mScratch = mScratch,
            .mFresh = step.mFresh[Temporal::Accumulate],
        };
    }

    DenoiseHistory::ShadowImages DenoiseHistory::shadow(const TemporalTurns::Step& step) const
    {
        return ShadowImages{
            .mMomentsBefore = mShadowMoments.before(step),
            .mMoments = mShadowMoments.now(step),
            .mHistory = mShadowHistory,
            .mScratch = mShadowScratch,
            .mVisibility = mShadowVisibility,
            .mTiles = mShadowTiles,
            .mMask = mShadowMask,
            .mHeldSurface = mSurface.before(step),
            .mFresh = step.mFresh[Temporal::Shadow],
        };
    }

    DenoiseHistory::SpecularImages DenoiseHistory::specular(const TemporalTurns::Step& step) const
    {
        return SpecularImages{
            .mMeanBefore = mSpecularMeans.before(step),
            .mMean = mSpecularMeans.now(step),
            .mHeldSurface = mSurface.before(step),
            .mFresh = step.mFresh[Temporal::Specular],
        };
    }

    DenoiseHistory::PaneImages DenoiseHistory::pane(const TemporalTurns::Step& step) const
    {
        return PaneImages{
            .mMeanBefore = mPaneMeans.before(step),
            .mHeldBefore = mPaneHeld.before(step),
            .mMean = mPaneMeans.now(step),
            .mHeld = mPaneHeld.now(step),
            .mFresh = step.mFresh[Temporal::Pane],
        };
    }
}
