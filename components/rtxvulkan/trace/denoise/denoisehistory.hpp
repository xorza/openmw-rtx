#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/trace/tracepast.hpp>

#include "temporalturns.hpp"

namespace Rtx
{
    class Device;

    /// Every image one camera's denoisers keep, named once: the table in `denoisehistory.cpp` gives
    /// each its format, its role, its grid and the filter it belongs to.
    enum class DenoiseImage : std::uint8_t
    {
        Surface,
        Colour,
        Moments,
        Blended,
        Narrow,
        Fill,
        FillBlended,
        FillNarrow,
        Fast,
        FastBlended,
        SkyShadowMoments,
        SkyShadowHistory,
        SkyShadowScratch,
        SkyShadowVisibility,
        SkyShadowTiles,
        SkyShadowPenumbra,
        SkyShadowMask,
        LampShadowMoments,
        LampShadowHistory,
        LampShadowScratch,
        LampShadowVisibility,
        LampShadowTiles,
        LampShadowPenumbra,
        LampShadowMask,
        SpecularMean,
        SpecularFast,
        SpecularFastBlended,
        PaneMean,
        PaneHeld,
        PaneFast,
        PaneFastBlended,
    };

    inline constexpr std::size_t sDenoiseImages = static_cast<std::size_t>(DenoiseImage::PaneFastBlended) + 1;

    /// The shadow denoiser's two fields: the sun's or a moon's rays, `CHANNEL_SHADOWED`, and the
    /// lamps', `CHANNEL_LAMPED`. Each has a history of its own, and the composite scales each light
    /// by its own field.
    enum class ShadowField : std::uint8_t
    {
        Sky,
        Lamps,
    };

    inline constexpr std::size_t sShadowFields = static_cast<std::size_t>(ShadowField::Lamps) + 1;

    /// Everything one camera's denoisers keep, at one extent: the five temporal filters' histories,
    /// what each writes of a frame's own, and the wavelet's scratch. A chain's and not the passes',
    /// because the passes are pipelines every chain shares and a history is as big as the camera it
    /// follows.
    ///
    /// **One owner, one turn and one discard.** Each filter's images are handed to its pass as one
    /// struct, built from the frame's turn, and what the frame writes whole — and, where a history
    /// is fresh, what it would have read — is discarded in one barrier ahead of every pass. **One
    /// table**, which makes every image, discards it by its role and checks its format against that
    /// role where it is written: a history read back into its own blend is never stored in a format
    /// whose store may round toward nought (`Shaders::mayRoundTowardNought`).
    class DenoiseHistory
    {
    public:
        /// `use` is what the images are counted as, as `GBuffer` takes it. Where `past` drops what a
        /// trace leaves, a pair is one image, which a frame both reads as last frame's and writes,
        /// and every frame must be fresh, so that it reads none.
        DenoiseHistory(const Device& device, MemoryUse use, TracePast past);

        /// What the history of a frame this size takes of the device's memory.
        static VkDeviceSize bytesAt(const Device& device, std::uint32_t width, std::uint32_t height, TracePast past);

        /// Makes room for a frame this size, anew: `TraceChain::resize` is what asks whether the size
        /// changed. A resize is a reset.
        void resize(std::uint32_t width, std::uint32_t height);

        /// Lets every image go, until the next `resize`.
        void release() { mImages = {}; }

        /// Says every history is worthless, until each filter next runs.
        void reset() { mTurns.reset(); }

        /// What a world distance is multiplied by before a surface history holds it, for a frame
        /// whose far plane is `far`: `HistoryConstants::mDistanceScale`, which says why.
        static float distanceScaleFor(float far);

        /// Turns to the other half of every pair for the frame being recorded, on which `runs` run:
        /// which halves each filter reads and writes, and which have nothing to read.
        TemporalTurns::Step turn(const TemporalFlags& runs);

        /// Discards, in one barrier, what the filters that run this frame write whole, and what the
        /// fresh ones among them would have read. The last frame's accesses to all of it are behind
        /// the head barrier `CommandPool::begin` recorded.
        ///
        /// **Discarding a history that holds nothing is what makes a reset a statement about the
        /// history and not about the memory**: the first frame after a resize has nothing behind it,
        /// and an image nothing wrote is whatever the allocation held, in no layout at all.
        void discard(VkCommandBuffer commands, const TemporalTurns::Step& step) const;

        /// The accumulator's and the wavelet's images: the histories the accumulator reads and
        /// writes, the blend it hands the cascade, and the narrow image its later levels ping-pong
        /// through with the blend.
        struct AccumulateImages
        {
            /// The surface last frame's histories belong to, as every temporal pass that asks
            /// whether a texel is still the same surface reads it (`heldSurfaceMatches`): the normal
            /// and the distance the accumulator wrote for each pixel. **One surface history for
            /// every pass that asks**, since each asks it of the same pixels of the same frames; the
            /// accumulator writes it, and the shadow denoiser and the glossy filter only read it.
            const Image& mSurfaceBefore;
            const Image& mMomentsBefore;

            /// Read by the accumulator as last frame's and written by the cascade's first level — SVGF's
            /// feedback, so what carries forward is the filtered light. One image and not a pair: the
            /// read is behind the frame's barriers before the write.
            const Image& mColour;
            const Image& mSurface;
            const Image& mMoments;

            /// This frame's bounce blended with the history, which the cascade filters. One image
            /// and not a pair, because nothing reads it after the frame that wrote it.
            const Image& mBlended;

            /// What the levels after the first write by turns with the blend, which nothing reads
            /// once the first level has.
            const Image& mNarrow;

            /// The share of the bounce that is the fill, through the same three: the mean, as
            /// `mColour` is held, the blend and the narrow image.
            const Image& mFill;
            const Image& mFillBlended;
            const Image& mFillNarrow;

            /// The fast means of the bounce and the fill (`ACCUMULATE_FAST`), which the accumulator
            /// reads as last frame's and the clamp writes, and the accumulator's blend of them, which
            /// the clamp reads at a pixel's neighbours as it writes the pixel's mean.
            const Image& mFast;
            const Image& mFastBlended;

            bool mFresh;
        };

        /// One field of the shadow denoiser's: **six images of a frame's own and two that carry
        /// over**, the SDK's arrangement and the penumbra beside it. The mask pass packs the rays'
        /// bits and the tiles' penumbra, the temporal pass writes its blend into the scratch, the
        /// first filter level writes the history the next frame's temporal pass reads, the second
        /// writes the scratch again — where a cleared tile keeps the temporal pass's exact value —
        /// and the third writes what the composite reads.
        struct ShadowImages
        {
            const Image& mMomentsBefore;
            const Image& mMoments;
            const Image& mHistory;
            const Image& mScratch;
            const Image& mVisibility;

            /// One texel a tile of the classification, `SHADOW_WORKGROUP` pixels on a side, and one
            /// of the widest penumbra in the tile, which the mask pass writes.
            const Image& mTiles;
            const Image& mPenumbra;

            /// One word an 8×4 tile of the rays' bits.
            const Image& mMask;

            /// `AccumulateImages::mSurfaceBefore`.
            const Image& mHeldSurface;

            /// Which rays' bits these filter.
            ShadowField mField;

            bool mFresh;
        };

        /// The glossy filter's: the mean and its frame count, the last frame's and this one's; and
        /// the fast mean (`HISTORY_CLAMP_FAST`), which the filter reads as last frame's and the clamp
        /// writes, and the filter's blend of it, which the clamp reads at a pixel's neighbours as it
        /// writes the pixel's.
        struct SpecularImages
        {
            const Image& mMeanBefore;
            const Image& mMean;
            const Image& mFast;
            const Image& mFastBlended;

            /// `AccumulateImages::mSurfaceBefore`.
            const Image& mHeldSurface;

            bool mFresh;
        };

        /// The pane filter's: the mean with its frame count, and the layer it belongs to, the last
        /// frame's and this one's; and the fast mean, as the glossy filter keeps it.
        struct PaneImages
        {
            const Image& mMeanBefore;
            const Image& mHeldBefore;
            const Image& mMean;
            const Image& mHeld;
            const Image& mFast;
            const Image& mFastBlended;
            bool mFresh;
        };

        AccumulateImages accumulate(const TemporalTurns::Step& step) const;
        ShadowImages shadow(ShadowField field, const TemporalTurns::Step& step) const;
        SpecularImages specular(const TemporalTurns::Step& step) const;
        PaneImages pane(const TemporalTurns::Step& step) const;

    private:
        const Device& mDevice;
        MemoryUse mUse;
        TracePast mPast;

        /// The half of a pair the last frame wrote, the half this frame writes, and the one image of
        /// what is not a pair.
        const Image& before(DenoiseImage image, const TemporalTurns::Step& step) const;
        const Image& now(DenoiseImage image, const TemporalTurns::Step& step) const;
        const Image& only(DenoiseImage image) const;

        /// Indexed by `DenoiseImage`: both halves of a pair, the first alone of what is not one or
        /// where the past is dropped. Empty until `resize`.
        std::array<std::array<Image, 2>, sDenoiseImages> mImages;

        /// Started again by `resize`, so the first frame after one reads no image nothing wrote.
        TemporalTurns mTurns;
    };
}
