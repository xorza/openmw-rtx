#include "denoisehistory.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
            /// filtered history. Never stored where a store may round toward nought, unless the shader
            /// rounds every value it stores there first (`Store::RoundedAtRandom`).
            FedBack,

            /// One frame's geometry, which the next frame reads and no frame blends: a held surface.
            OneFrame,

            /// Read by nothing after the frame that wrote it, but a step of a history's loop: what a
            /// history is blended into and filtered from before the next frame reads it back. Never
            /// stored where a store may round toward nought, for the reason `FedBack` is not.
            InLoop,

            /// Read by nothing after the frame that wrote it, and by no history.
            Scratch,
        };

        /// What a shader hands an image's store: its value as it computed it, or that value rounded
        /// to one the format holds exactly, so what the store does with what it is handed decides
        /// nothing — at random (`roundedToHalf`), which a blend may read back, or to the nearest
        /// (`nearestHalf`), which none may.
        enum class Store : std::uint8_t
        {
            AsComputed,
            RoundedToNearest,
            RoundedAtRandom,
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

            /// Two halves, the last frame's and this one's, by `TemporalTurns::Step`, where one dispatch
            /// reads last frame's at its neighbours' texels while it writes this frame's. One image
            /// where nothing reads it on the next frame, or where the frame reads it before a later pass
            /// writes it again: the shadow's history, which its temporal pass reads and its first
            /// filter level writes; the bounce's and the fill's means, which the accumulator reads and
            /// the cascade's first level writes; and each fast mean, which a filter reads and its clamp
            /// writes.
            bool mPair;

            Grid mGrid;
            VkImageUsageFlags mUsage;

            /// The filter whose history it is: what its freshness follows.
            Temporal mFilter;

            Store mStore = Store::AsComputed;
        };

        constexpr std::array<Declared, sDenoiseImages> sDeclared{ {
            { DenoiseImage::Colour, "accumulate-colour", ACCUMULATE_COLOUR, Role::FedBack, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate, Store::RoundedAtRandom },
            { DenoiseImage::Moments, "accumulate-moments", ACCUMULATE_MOMENTS, Role::FedBack, true, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate, Store::RoundedAtRandom },
            // And the second narrow level's target, rounded to the nearest, after the first level
            // has read it (`AtrousPass::record`).
            { DenoiseImage::Blended, "accumulate-blended", ATROUS_CHANNEL, Role::InLoop, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate, Store::RoundedAtRandom },
            { DenoiseImage::Narrow, "atrous-narrow", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels, sReadAndWrite,
                Temporal::Accumulate, Store::RoundedToNearest },
            { DenoiseImage::Fill, "accumulate-fill", ACCUMULATE_COLOUR, Role::FedBack, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate, Store::RoundedAtRandom },
            { DenoiseImage::FillBlended, "accumulate-fill-blended", ATROUS_CHANNEL, Role::InLoop, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate, Store::RoundedAtRandom },
            { DenoiseImage::FillNarrow, "atrous-fill-narrow", ATROUS_CHANNEL, Role::Scratch, false, Grid::Pixels,
                sReadAndWrite, Temporal::Accumulate, Store::RoundedToNearest },
            { DenoiseImage::Fast, "accumulate-fast", ACCUMULATE_FAST, Role::FedBack, false, Grid::Pixels, sStorage,
                Temporal::Accumulate },
            { DenoiseImage::FastBlended, "accumulate-fast-blended", ACCUMULATE_FAST, Role::InLoop, false, Grid::Pixels,
                sStorage, Temporal::Accumulate },
            { DenoiseImage::SkyShadowMoments, "sky-shadow-moments", SHADOW_MOMENTS, Role::FedBack, true, Grid::Pixels,
                sStorage, Temporal::SkyShadow, Store::RoundedAtRandom },
            { DenoiseImage::SkyShadowHistory, "sky-shadow-history", SHADOW_REPROJECTED, Role::FedBack, false,
                Grid::Pixels, sStorage, Temporal::SkyShadow, Store::RoundedAtRandom },
            // And the second filter level's target, rounded to the nearest, after the first level has
            // read it (`shadowfilter.comp`).
            { DenoiseImage::SkyShadowScratch, "sky-shadow-scratch", SHADOW_REPROJECTED, Role::InLoop, false,
                Grid::Pixels, sStorage, Temporal::SkyShadow, Store::RoundedAtRandom },
            { DenoiseImage::SkyShadowVisibility, "sky-shadow-visibility", SHADOW_VISIBILITY, Role::Scratch, false,
                Grid::Pixels, sStorage, Temporal::SkyShadow },
            { DenoiseImage::SkyShadowTiles, "sky-shadow-tiles", SHADOW_TILES, Role::Scratch, false, Grid::ShadowTiles,
                sStorage, Temporal::SkyShadow },
            { DenoiseImage::SkyShadowPenumbra, "sky-shadow-penumbra", SHADOW_PENUMBRA_TILES, Role::Scratch, false,
                Grid::ShadowTiles, sStorage, Temporal::SkyShadow },
            { DenoiseImage::SkyShadowMask, "sky-shadow-mask", SHADOW_MASK, Role::Scratch, false, Grid::ShadowMask,
                sStorage, Temporal::SkyShadow },
            { DenoiseImage::LampShadowMoments, "lamp-shadow-moments", SHADOW_MOMENTS, Role::FedBack, true, Grid::Pixels,
                sStorage, Temporal::LampShadow, Store::RoundedAtRandom },
            { DenoiseImage::LampShadowHistory, "lamp-shadow-history", SHADOW_REPROJECTED, Role::FedBack, false,
                Grid::Pixels, sStorage, Temporal::LampShadow, Store::RoundedAtRandom },
            // And the second filter level's target, rounded to the nearest, after the first level has
            // read it (`shadowfilter.comp`).
            { DenoiseImage::LampShadowScratch, "lamp-shadow-scratch", SHADOW_REPROJECTED, Role::InLoop, false,
                Grid::Pixels, sStorage, Temporal::LampShadow, Store::RoundedAtRandom },
            { DenoiseImage::LampShadowVisibility, "lamp-shadow-visibility", SHADOW_VISIBILITY, Role::Scratch, false,
                Grid::Pixels, sStorage, Temporal::LampShadow },
            { DenoiseImage::LampShadowTiles, "lamp-shadow-tiles", SHADOW_TILES, Role::Scratch, false, Grid::ShadowTiles,
                sStorage, Temporal::LampShadow },
            { DenoiseImage::LampShadowPenumbra, "lamp-shadow-penumbra", SHADOW_PENUMBRA_TILES, Role::Scratch, false,
                Grid::ShadowTiles, sStorage, Temporal::LampShadow },
            { DenoiseImage::LampShadowMask, "lamp-shadow-mask", SHADOW_MASK, Role::Scratch, false, Grid::ShadowMask,
                sStorage, Temporal::LampShadow },
            { DenoiseImage::SpecularMean, "specular-mean", SPECULAR_MEAN, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Specular, Store::RoundedAtRandom },
            { DenoiseImage::SpecularFast, "specular-fast", HISTORY_CLAMP_FAST, Role::FedBack, false, Grid::Pixels,
                sStorage, Temporal::Specular },
            { DenoiseImage::SpecularFastBlended, "specular-fast-blended", HISTORY_CLAMP_FAST, Role::InLoop, false,
                Grid::Pixels, sStorage, Temporal::Specular },
            { DenoiseImage::PaneMean, "pane-mean", PANE_MEAN, Role::FedBack, true, Grid::Pixels, sStorage,
                Temporal::Pane, Store::RoundedAtRandom },
            { DenoiseImage::PaneFast, "pane-fast", HISTORY_CLAMP_FAST, Role::FedBack, false, Grid::Pixels, sStorage,
                Temporal::Pane },
            { DenoiseImage::PaneFastBlended, "pane-fast-blended", HISTORY_CLAMP_FAST, Role::InLoop, false, Grid::Pixels,
                sStorage, Temporal::Pane },
        } };

        constexpr bool inOrder()
        {
            for (std::size_t at = 0; at < sDeclared.size(); ++at)
                if (static_cast<std::size_t>(sDeclared[at].mImage) != at)
                    return false;
            return true;
        }

        constexpr bool loopsKeepTheirPrecision()
        {
            for (const Declared& declared : sDeclared)
                if ((declared.mRole == Role::FedBack || declared.mRole == Role::InLoop)
                    && Shaders::mayRoundTowardNought(declared.mFormat) && declared.mStore != Store::RoundedAtRandom)
                    return false;
            return true;
        }

        static_assert(inOrder(), "the table is indexed by DenoiseImage");
        static_assert(
            loopsKeepTheirPrecision(), "a step of a history's loop is stored where a store may round toward nought");

        /// The rows each of the shadow's fields takes, indexed by `ShadowField`.
        struct ShadowRows
        {
            DenoiseImage mMoments;
            DenoiseImage mHistory;
            DenoiseImage mScratch;
            DenoiseImage mVisibility;
            DenoiseImage mTiles;
            DenoiseImage mPenumbra;
            DenoiseImage mMask;
            Temporal mFilter;
        };

        constexpr std::array<ShadowRows, sShadowFields> sShadowRows{ {
            { DenoiseImage::SkyShadowMoments, DenoiseImage::SkyShadowHistory, DenoiseImage::SkyShadowScratch,
                DenoiseImage::SkyShadowVisibility, DenoiseImage::SkyShadowTiles, DenoiseImage::SkyShadowPenumbra,
                DenoiseImage::SkyShadowMask, Temporal::SkyShadow },
            { DenoiseImage::LampShadowMoments, DenoiseImage::LampShadowHistory, DenoiseImage::LampShadowScratch,
                DenoiseImage::LampShadowVisibility, DenoiseImage::LampShadowTiles, DenoiseImage::LampShadowPenumbra,
                DenoiseImage::LampShadowMask, Temporal::LampShadow },
        } };

        constexpr const Declared& declaredOf(const DenoiseImage image)
        {
            return sDeclared[static_cast<std::size_t>(image)];
        }

        constexpr bool shadowRowsAreTheirFields()
        {
            for (const ShadowRows& rows : sShadowRows)
                for (const DenoiseImage image : { rows.mMoments, rows.mHistory, rows.mScratch, rows.mVisibility,
                         rows.mTiles, rows.mPenumbra, rows.mMask })
                    if (declaredOf(image).mFilter != rows.mFilter)
                        return false;
            return true;
        }

        static_assert(shadowRowsAreTheirFields(), "a shadow field takes an image whose freshness is another's");

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

    DenoiseHistory::DenoiseHistory(const Device& device, const MemoryUse use, const TracePast past)
        : mDevice(device)
        , mUse(use)
        , mPast(past)
    {
    }

    VkDeviceSize DenoiseHistory::bytesAt(
        const Device& device, const std::uint32_t width, const std::uint32_t height, const TracePast past)
    {
        VkDeviceSize bytes = 0;
        for (const Declared& declared : sDeclared)
            bytes += Image::bytesFor(device, descriptionOf(declared, width, height))
                * (declared.mPair && past == TracePast::Kept ? 2 : 1);
        return bytes;
    }

    void DenoiseHistory::resize(const std::uint32_t width, const std::uint32_t height)
    {
        for (const Declared& declared : sDeclared)
        {
            const ImageDescription description = descriptionOf(declared, width, height);
            std::array<Image, 2>& images = mImages[static_cast<std::size_t>(declared.mImage)];
            if (!declared.mPair || mPast == TracePast::Dropped)
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
        return mImages[static_cast<std::size_t>(image)][mPast == TracePast::Kept ? step.mBefore : 0];
    }

    const Image& DenoiseHistory::now(const DenoiseImage image, const TemporalTurns::Step& step) const
    {
        assert(declaredOf(image).mPair && "this frame's half of what is not a pair");
        return mImages[static_cast<std::size_t>(image)][mPast == TracePast::Kept ? step.mNow : 0];
    }

    const Image& DenoiseHistory::only(const DenoiseImage image) const
    {
        assert(!declaredOf(image).mPair && "one image of a pair");
        return mImages[static_cast<std::size_t>(image)][0];
    }

    TemporalTurns::Step DenoiseHistory::turn(const TemporalFlags& runs)
    {
        assert(!mImages[static_cast<std::size_t>(DenoiseImage::Colour)][0].isEmpty() && "a turn before resize");
        const TemporalTurns::Step step = mTurns.next(runs);
        for (std::size_t at = 0; at < sTemporals; ++at)
            assert((mPast == TracePast::Kept || !step.mRuns.mFlags[at] || step.mFresh.mFlags[at])
                && "a history read where the past is dropped and a pair is one image");
        return step;
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
            if (declared.mPair && mPast == TracePast::Kept)
            {
                now(declared.mImage, step).addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
                if (fresh)
                    before(declared.mImage, step).addTransition(barriers, Use::sUndefined, Use::sComputeRead);
            }
            // One image both halves name: the frame writes it whole, and a pass binds it as last
            // frame's too, which a fresh history never reads, but which a binding states.
            else if (declared.mPair)
                now(declared.mImage, step).addTransition(barriers, Use::sUndefined, Use::sComputeReadWrite);
            else if (declared.mRole == Role::Scratch || declared.mRole == Role::InLoop)
                only(declared.mImage).addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
            else if (fresh)
                only(declared.mImage).addTransition(barriers, Use::sUndefined, Use::sComputeRead);
        }

        // **A shadow field that does not run beside one that does is bound all the same**: one module
        // filters both fields (`ShadowPass::recordLevel`) and names each one's images, which a binding
        // states in its layout though the module never reads them. Laid out for it every such frame:
        // what they held is nothing the field reads again, since one that does not run is fresh the
        // next time it does.
        const bool shadowRuns = step.mRuns[Temporal::SkyShadow] || step.mRuns[Temporal::LampShadow];
        for (const ShadowRows& rows : sShadowRows)
            if (shadowRuns && !step.mRuns[rows.mFilter])
                for (const DenoiseImage image : { rows.mHistory, rows.mScratch, rows.mVisibility, rows.mTiles })
                    only(image).addTransition(barriers, Use::sUndefined, Use::sComputeRead);

        barriers.flush();
    }

    DenoiseHistory::AccumulateImages DenoiseHistory::accumulate(const TemporalTurns::Step& step) const
    {
        return AccumulateImages{
            .mMomentsBefore = before(DenoiseImage::Moments, step),
            .mColour = only(DenoiseImage::Colour),
            .mMoments = now(DenoiseImage::Moments, step),
            .mBlended = only(DenoiseImage::Blended),
            .mNarrow = only(DenoiseImage::Narrow),
            .mFill = only(DenoiseImage::Fill),
            .mFillBlended = only(DenoiseImage::FillBlended),
            .mFillNarrow = only(DenoiseImage::FillNarrow),
            .mFast = only(DenoiseImage::Fast),
            .mFastBlended = only(DenoiseImage::FastBlended),
            .mFresh = step.mFresh[Temporal::Accumulate],
        };
    }

    DenoiseHistory::ShadowImages DenoiseHistory::shadow(const ShadowField field, const TemporalTurns::Step& step) const
    {
        const ShadowRows& rows = sShadowRows[static_cast<std::size_t>(field)];
        return ShadowImages{
            .mMomentsBefore = before(rows.mMoments, step),
            .mMoments = now(rows.mMoments, step),
            .mHistory = only(rows.mHistory),
            .mScratch = only(rows.mScratch),
            .mVisibility = only(rows.mVisibility),
            .mTiles = only(rows.mTiles),
            .mPenumbra = only(rows.mPenumbra),
            .mMask = only(rows.mMask),
            .mField = field,
            .mFresh = step.mFresh[rows.mFilter],
        };
    }

    DenoiseHistory::SpecularImages DenoiseHistory::specular(const TemporalTurns::Step& step) const
    {
        return SpecularImages{
            .mMeanBefore = before(DenoiseImage::SpecularMean, step),
            .mMean = now(DenoiseImage::SpecularMean, step),
            .mFast = only(DenoiseImage::SpecularFast),
            .mFastBlended = only(DenoiseImage::SpecularFastBlended),
            .mFresh = step.mFresh[Temporal::Specular],
        };
    }

    DenoiseHistory::PaneImages DenoiseHistory::pane(const TemporalTurns::Step& step) const
    {
        return PaneImages{
            .mMeanBefore = before(DenoiseImage::PaneMean, step),
            .mMean = now(DenoiseImage::PaneMean, step),
            .mFast = only(DenoiseImage::PaneFast),
            .mFastBlended = only(DenoiseImage::PaneFastBlended),
            .mFresh = step.mFresh[Temporal::Pane],
        };
    }
}
