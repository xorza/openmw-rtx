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
#include <components/rtxvulkan/shaders/shared/historyclamp.h>
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

            /// The filter whose history it is: what its freshness follows.
            Temporal mFilter;
        };

        constexpr std::array<Declared, sDenoiseImages> sDeclared{ {
            { DenoiseImage::Surface, "accumulate-surface", ACCUMULATE_SURFACE, Role::OneFrame, true, Grid::Pixels,
                sStorage, Temporal::Accumulate },
            { DenoiseImage::Colour, "accumulate-colour", ACCUMULATE_COLOUR, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::Moments, "accumulate-moments", ACCUMULATE_MOMENTS, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::Blended, "accumulate-blended", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::Narrow, "atrous-narrow", ATROUS_NARROW, Role::Scratch, false, Grid::Pixels, sReadAndWrite,
                Temporal::Accumulate },
            { DenoiseImage::NarrowOther, "atrous-narrow-other", ATROUS_NARROW, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::Fill, "accumulate-fill", ACCUMULATE_COLOUR, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::FillBlended, "accumulate-fill-blended", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::FillNarrow, "atrous-fill-narrow", ATROUS_NARROW, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::FillNarrowOther, "atrous-fill-narrow-other", ATROUS_NARROW, Role::Scratch, false,
                Grid::Pixels, sReadAndWrite, Temporal::Accumulate },
            { DenoiseImage::Fast, "accumulate-fast", ACCUMULATE_FAST, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Accumulate },
            { DenoiseImage::FastBlended, "accumulate-fast-blended", ACCUMULATE_FAST, Role::Scratch, false, Grid::Pixels,
                sStorage, Temporal::Accumulate },
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
            { DenoiseImage::SpecularFast, "specular-fast", HISTORY_CLAMP_FAST, Role::FedBack, true, Grid::Pixels,
                sStorage, Temporal::Specular },
            { DenoiseImage::SpecularFastBlended, "specular-fast-blended", HISTORY_CLAMP_FAST, Role::Scratch, false,
                Grid::Pixels, sStorage, Temporal::Specular },
            { DenoiseImage::PaneMean, "pane-mean", PANE_MEAN, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Pane },
            { DenoiseImage::PaneHeld, "pane-held", ACCUMULATE_SURFACE, Role::OneFrame, true, Grid::Pixels, sStorage,
                Temporal::Pane },
            { DenoiseImage::PaneFast, "pane-fast", HISTORY_CLAMP_FAST, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Pane },
            { DenoiseImage::PaneFastBlended, "pane-fast-blended", HISTORY_CLAMP_FAST, Role::Scratch, false,
                Grid::Pixels, sStorage, Temporal::Pane },
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

        /// What `declared` is made of for a frame `width` by `height`: its grid's extent and its
        /// format, which `resize` makes and `bytesAt` measures.
        ImageDescription descriptionOf(const Declared& declared, const std::uint32_t width, const std::uint32_t height)
        {
            std::uint32_t columns = width;
            std::uint32_t rows = height;
            if (declared.mGrid == Grid::ShadowTiles)
            {
                columns = groupsFor(width, Shaders::SHADOW_WORKGROUP);
                rows = groupsFor(height, Shaders::SHADOW_WORKGROUP);
            }
            else if (declared.mGrid == Grid::ShadowMask)
            {
                columns = groupsFor(width, Shaders::SHADOW_MASK_WIDTH);
                rows = groupsFor(height, Shaders::SHADOW_MASK_HEIGHT);
            }

            return ImageDescription{ .mWidth = columns,
                .mHeight = rows,
                .mFormat = toVulkanFormat(declared.mFormat),
                .mUsage = declared.mUsage };
        }
    }

    DenoiseHistory::DenoiseHistory(const Device& device, const MemoryUse use)
        : mDevice(device)
        , mUse(use)
    {
    }

    VkDeviceSize DenoiseHistory::bytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        VkDeviceSize bytes = 0;
        for (const Declared& declared : sDeclared)
            bytes += Image::bytesFor(device, descriptionOf(declared, width, height)) * (declared.mPair ? 2 : 1);
        return bytes;
    }

    float DenoiseHistory::distanceScaleFor(const float far)
    {
        assert(far > 0.0f && "a frame with no far plane to scale a stored distance by");
        return Shaders::ACCUMULATE_DISTANCE_RANGE / far;
    }

    void DenoiseHistory::resize(const std::uint32_t width, const std::uint32_t height)
    {
        for (const Declared& declared : sDeclared)
        {
            const ImageDescription description = descriptionOf(declared, width, height);
            std::array<Image, 2>& images = mImages[static_cast<std::size_t>(declared.mImage)];
            if (!declared.mPair)
            {
                images[0] = Image(mUse, mDevice, description, declared.mName);
                continue;
            }

            for (std::size_t half = 0; half < images.size(); ++half)
                images[half]
                    = Image(mUse, mDevice, description, std::string(declared.mName) + "-" + std::to_string(half));
        }

        mTurns = TemporalTurns{};
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
            .mNarrow = only(DenoiseImage::Narrow),
            .mNarrowOther = only(DenoiseImage::NarrowOther),
            .mFillBefore = before(DenoiseImage::Fill, step),
            .mFill = now(DenoiseImage::Fill, step),
            .mFillBlended = only(DenoiseImage::FillBlended),
            .mFillNarrow = only(DenoiseImage::FillNarrow),
            .mFillNarrowOther = only(DenoiseImage::FillNarrowOther),
            .mFastBefore = before(DenoiseImage::Fast, step),
            .mFast = now(DenoiseImage::Fast, step),
            .mFastBlended = only(DenoiseImage::FastBlended),
            .mFresh = step.mFresh[Temporal::Accumulate],
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
            .mFastBefore = before(DenoiseImage::SpecularFast, step),
            .mFast = now(DenoiseImage::SpecularFast, step),
            .mFastBlended = only(DenoiseImage::SpecularFastBlended),
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
            .mFastBefore = before(DenoiseImage::PaneFast, step),
            .mFast = now(DenoiseImage::PaneFast, step),
            .mFastBlended = only(DenoiseImage::PaneFastBlended),
            .mFresh = step.mFresh[Temporal::Pane],
        };
    }
}
