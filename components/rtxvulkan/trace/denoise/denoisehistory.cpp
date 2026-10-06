#include "denoisehistory.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>
#include <components/rtxvulkan/shaders/shared/atrous.h>
#include <components/rtxvulkan/shaders/shared/pane.h>
#include <components/rtxvulkan/shaders/shared/shadow.h>
#include <components/rtxvulkan/shaders/shared/specular.h>

namespace Rtx
{
    namespace
    {
        constexpr VkImageUsageFlags sStorage = VK_IMAGE_USAGE_STORAGE_BIT;

        /// `SAMPLED` beside `STORAGE` on what the cascade reads. `AtrousPass` takes its taps
        /// through the texture unit, and a sampled descriptor needs the bit at creation.
        constexpr VkImageUsageFlags sReadAndWrite = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        /// What an image holds from one frame to the next, which is what decides how a frame
        /// discards it and which formats it may be stored in.
        enum class Role : std::uint8_t
        {
            /// Read back into its own blend on the next frame: a running mean, its moments, a
            /// filtered history. Never stored where a store may round toward nought.
            FedBack,

            /// One frame's geometry, which the next frame reads and no frame blends: a held surface.
            OneFrame,

            /// Read by nothing after the frame that wrote it.
            Scratch,
        };

        /// Which grid an image is made on: one texel a pixel, a tile of the shadow's classification,
        /// or a word of its mask.
        enum class Grid : std::uint8_t
        {
            Pixels,
            ShadowTiles,
            ShadowMask,
        };

        /// One row of the table.
        struct Declared
        {
            DenoiseImage mImage;
            std::string_view mName;
            Shaders::StorageFormat mFormat;
            Role mRole;

            /// Two halves, the last frame's and this one's, by `TemporalTurns::Step`. One image where
            /// nothing reads it on the next frame, or where the frame reads it before a later pass
            /// writes it again — the shadow's history, which its temporal pass reads and its first
            /// filter level writes.
            bool mPair;

            Grid mGrid;
            VkImageUsageFlags mUsage;

            /// The filter whose history it is: what its freshness follows, and the bounce's are what
            /// `keepBounce` makes and lets go.
            Temporal mFilter;
        };

        constexpr std::array<Declared, sDenoiseImages> sDeclared{ {
            { DenoiseImage::Surface, "accumulate-surface", ACCUMULATE_SURFACE, Role::OneFrame, true, Grid::Pixels,
                sStorage, Temporal::Accumulate },
            { DenoiseImage::Colour, "accumulate-colour", ACCUMULATE_COLOUR, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::Moments, "accumulate-moments", ACCUMULATE_MOMENTS, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::Blended, "accumulate-blended", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::Scratch, "atrous-scratch", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::Fill, "accumulate-fill", ACCUMULATE_COLOUR, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::FillBlended, "accumulate-fill-blended", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::FillScratch, "atrous-fill-scratch", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Bounce },
            { DenoiseImage::Fast, "accumulate-fast", ACCUMULATE_FAST, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Bounce },
            { DenoiseImage::FastBlended, "accumulate-fast-blended", ACCUMULATE_FAST, Role::Scratch, false, Grid::Pixels,
                sStorage, Temporal::Bounce },
            { DenoiseImage::ShadowMoments, "shadow-moments", SHADOW_MOMENTS, Role::FedBack, true, Grid::Pixels,
                sStorage, Temporal::Shadow },
            { DenoiseImage::ShadowHistory, "shadow-history", SHADOW_REPROJECTED, Role::FedBack, false, Grid::Pixels,
                sStorage, Temporal::Shadow },
            { DenoiseImage::ShadowScratch, "shadow-scratch", SHADOW_REPROJECTED, Role::Scratch, false, Grid::Pixels,
                sStorage, Temporal::Shadow },
            { DenoiseImage::ShadowVisibility, "shadow-visibility", SHADOW_REPROJECTED, Role::Scratch, false,
                Grid::Pixels, sStorage, Temporal::Shadow },
            { DenoiseImage::ShadowTiles, "shadow-tiles", SHADOW_TILES, Role::Scratch, false, Grid::ShadowTiles,
                sStorage, Temporal::Shadow },
            { DenoiseImage::ShadowPenumbra, "shadow-penumbra", SHADOW_PENUMBRA_TILES, Role::Scratch, false,
                Grid::ShadowTiles, sStorage, Temporal::Shadow },
            { DenoiseImage::ShadowMask, "shadow-mask", SHADOW_MASK, Role::Scratch, false, Grid::ShadowMask, sStorage,
                Temporal::Shadow },
            { DenoiseImage::SpecularMean, "specular-mean", SPECULAR_MEAN, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Specular },
            { DenoiseImage::PaneMean, "pane-mean", PANE_MEAN, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Pane },
            { DenoiseImage::PaneHeld, "pane-held", ACCUMULATE_SURFACE, Role::OneFrame, true, Grid::Pixels, sStorage,
                Temporal::Pane },
        } };

        constexpr bool inOrder()
        {
            for (std::size_t at = 0; at < sDeclared.size(); ++at)
                if (static_cast<std::size_t>(sDeclared[at].mImage) != at)
                    return false;
            return true;
        }

        constexpr bool fedBackKeepsItsPrecision()
        {
            for (const Declared& declared : sDeclared)
                if (declared.mRole == Role::FedBack && Shaders::mayRoundTowardNought(declared.mFormat))
                    return false;
            return true;
        }

        static_assert(inOrder(), "the table is indexed by DenoiseImage");
        static_assert(fedBackKeepsItsPrecision(),
            "a history read back into its own blend is stored where a store may round toward nought");

        constexpr const Declared& declaredOf(const DenoiseImage image)
        {
            return sDeclared[static_cast<std::size_t>(image)];
        }
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

    void DenoiseHistory::resize(const std::uint32_t width, const std::uint32_t height, const bool bounce)
    {
        mWidth = width;
        mHeight = height;

        for (std::size_t at = 0; at < sTemporals; ++at)
        {
            const Temporal filter = static_cast<Temporal>(at);
            if (filter != Temporal::Bounce || bounce)
                make(filter);
            else
                release(filter);
        }

        mTurns = TemporalTurns{};
    }

    void DenoiseHistory::keepBounce(const bool bounce)
    {
        assert(mWidth > 0 && "the bounce's images kept before a resize");
        if (bounce == !only(DenoiseImage::Blended).isEmpty())
            return;

        if (!bounce)
        {
            release(Temporal::Bounce);
            return;
        }

        make(Temporal::Bounce);

        // Made anew, so they hold nothing a frame may read, whichever frames ran before them.
        mTurns.reset(Temporal::Bounce);
    }

    void DenoiseHistory::make(const Temporal filter)
    {
        for (const Declared& declared : sDeclared)
        {
            if (declared.mFilter != filter)
                continue;

            std::uint32_t width = mWidth;
            std::uint32_t height = mHeight;
            if (declared.mGrid == Grid::ShadowTiles)
            {
                width = groupsFor(mWidth, Shaders::SHADOW_WORKGROUP);
                height = groupsFor(mHeight, Shaders::SHADOW_WORKGROUP);
            }
            else if (declared.mGrid == Grid::ShadowMask)
            {
                width = groupsFor(mWidth, Shaders::SHADOW_MASK_WIDTH);
                height = groupsFor(mHeight, Shaders::SHADOW_MASK_HEIGHT);
            }

            const VkFormat format = toVulkanFormat(declared.mFormat);
            std::array<Image, 2>& images = mImages[static_cast<std::size_t>(declared.mImage)];
            if (!declared.mPair)
            {
                images[0] = Image(mDevice, width, height, format, declared.mUsage, declared.mName);
                continue;
            }

            for (std::size_t half = 0; half < images.size(); ++half)
                images[half] = Image(mDevice, width, height, format, declared.mUsage,
                    std::string(declared.mName) + "-" + std::to_string(half));
        }
    }

    void DenoiseHistory::release(const Temporal filter)
    {
        for (const Declared& declared : sDeclared)
            if (declared.mFilter == filter)
                mImages[static_cast<std::size_t>(declared.mImage)] = {};
    }

    const Image& DenoiseHistory::before(const DenoiseImage image, const TemporalTurns::Step& step) const
    {
        assert(declaredOf(image).mPair && "the last frame's half of what is not a pair");
        return mImages[static_cast<std::size_t>(image)][step.mBefore];
    }

    const Image& DenoiseHistory::now(const DenoiseImage image, const TemporalTurns::Step& step) const
    {
        assert(declaredOf(image).mPair && "this frame's half of what is not a pair");
        return mImages[static_cast<std::size_t>(image)][step.mNow];
    }

    const Image& DenoiseHistory::only(const DenoiseImage image) const
    {
        assert(!declaredOf(image).mPair && "one image of a pair");
        return mImages[static_cast<std::size_t>(image)][0];
    }

    TemporalTurns::Step DenoiseHistory::turn(const TemporalFlags& runs)
    {
        assert(!mImages[static_cast<std::size_t>(DenoiseImage::Surface)][0].isEmpty() && "a turn before resize");
        assert((!runs[Temporal::Bounce] || !only(DenoiseImage::Blended).isEmpty())
            && "the bounce filtered with its images let go");
        return mTurns.next(runs);
    }

    void DenoiseHistory::discard(const VkCommandBuffer commands, const TemporalTurns::Step& step) const
    {
        // **By role**: this frame's half of a pair and a frame's own scratch are written whole; the
        // last frame's half, and a history one image keeps across the frame, are read, and hold
        // nothing where the history is fresh. The shadow's history is the first filter level's
        // answer from the frame before, so it is one of what a fresh history discards and not one
        // of what the frame writes whole.
        Barriers barriers(commands);
        for (const Declared& declared : sDeclared)
        {
            if (!step.mRuns[declared.mFilter])
                continue;

            const bool fresh = step.mFresh[declared.mFilter];
            if (declared.mPair)
            {
                now(declared.mImage, step).addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
                if (fresh)
                    before(declared.mImage, step).addTransition(barriers, Use::sUndefined, Use::sComputeRead);
            }
            else if (declared.mRole == Role::Scratch)
                only(declared.mImage).addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
            else if (fresh)
                only(declared.mImage).addTransition(barriers, Use::sUndefined, Use::sComputeRead);
        }

        barriers.flush();
    }

    DenoiseHistory::AccumulateImages DenoiseHistory::accumulate(const TemporalTurns::Step& step) const
    {
        return AccumulateImages{
            .mColourBefore = before(DenoiseImage::Colour, step),
            .mSurfaceBefore = before(DenoiseImage::Surface, step),
            .mMomentsBefore = before(DenoiseImage::Moments, step),
            .mColour = now(DenoiseImage::Colour, step),
            .mSurface = now(DenoiseImage::Surface, step),
            .mMoments = now(DenoiseImage::Moments, step),
            .mBlended = only(DenoiseImage::Blended),
            .mScratch = only(DenoiseImage::Scratch),
            .mFillBefore = before(DenoiseImage::Fill, step),
            .mFill = now(DenoiseImage::Fill, step),
            .mFillBlended = only(DenoiseImage::FillBlended),
            .mFillScratch = only(DenoiseImage::FillScratch),
            .mFastBefore = before(DenoiseImage::Fast, step),
            .mFast = now(DenoiseImage::Fast, step),
            .mFastBlended = only(DenoiseImage::FastBlended),
            .mFresh = step.mFresh[Temporal::Bounce],
        };
    }

    DenoiseHistory::ShadowImages DenoiseHistory::shadow(const TemporalTurns::Step& step) const
    {
        return ShadowImages{
            .mMomentsBefore = before(DenoiseImage::ShadowMoments, step),
            .mMoments = now(DenoiseImage::ShadowMoments, step),
            .mHistory = only(DenoiseImage::ShadowHistory),
            .mScratch = only(DenoiseImage::ShadowScratch),
            .mVisibility = only(DenoiseImage::ShadowVisibility),
            .mTiles = only(DenoiseImage::ShadowTiles),
            .mPenumbra = only(DenoiseImage::ShadowPenumbra),
            .mMask = only(DenoiseImage::ShadowMask),
            .mHeldSurface = before(DenoiseImage::Surface, step),
            .mFresh = step.mFresh[Temporal::Shadow],
        };
    }

    DenoiseHistory::SpecularImages DenoiseHistory::specular(const TemporalTurns::Step& step) const
    {
        return SpecularImages{
            .mMeanBefore = before(DenoiseImage::SpecularMean, step),
            .mMean = now(DenoiseImage::SpecularMean, step),
            .mHeldSurface = before(DenoiseImage::Surface, step),
            .mFresh = step.mFresh[Temporal::Specular],
        };
    }

    DenoiseHistory::PaneImages DenoiseHistory::pane(const TemporalTurns::Step& step) const
    {
        return PaneImages{
            .mMeanBefore = before(DenoiseImage::PaneMean, step),
            .mHeldBefore = before(DenoiseImage::PaneHeld, step),
            .mMean = now(DenoiseImage::PaneMean, step),
            .mHeld = now(DenoiseImage::PaneHeld, step),
            .mFresh = step.mFresh[Temporal::Pane],
        };
    }
}
