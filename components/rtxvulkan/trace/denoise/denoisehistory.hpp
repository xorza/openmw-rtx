#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/image.hpp>

#include "temporalturns.hpp"

namespace Rtx
{
    class Device;

    /// Two images of one history at one extent: the half the last frame wrote and the half this one
    /// writes, by `TemporalTurns::Step`.
    struct ImagePair
    {
        std::array<Image, 2> mImages;

        /// Both halves, named `name-0` and `name-1`.
        static ImagePair make(const Device& device, std::uint32_t width, std::uint32_t height, VkFormat format,
            VkImageUsageFlags usage, std::string_view name);

        const Image& before(const TemporalTurns::Step& step) const { return mImages[step.mBefore]; }
        const Image& now(const TemporalTurns::Step& step) const { return mImages[step.mNow]; }
    };

    /// Everything one camera's denoisers keep, at one extent: the four temporal filters' histories,
    /// what each writes of a frame's own, and the wavelet's scratch. A chain's and not the passes',
    /// because the passes are pipelines every chain shares and a history is as big as the camera it
    /// follows.
    ///
    /// **One owner, one turn and one discard.** Each filter's images are handed to its pass as one
    /// struct, built from the frame's turn, and what the frame writes whole — and, where a history
    /// is fresh, what it would have read — is discarded in one barrier ahead of every pass.
    class DenoiseHistory
    {
    public:
        explicit DenoiseHistory(const Device& device);

        /// Makes room for a frame this size, anew: `TraceChain::resize` is what asks whether the size
        /// changed. A resize is a reset.
        ///
        /// @param bounce whether the accumulator's mean of the bounce and the wavelet's images are
        ///        made: `Reconstruction::filtersBounce`, as the chain last heard it.
        void resize(std::uint32_t width, std::uint32_t height, bool bounce);

        /// Makes the bounce's images at the extent the history stands at, or lets them go, where
        /// `bounce` differs from what was made. **Images made anew are a fresh history**, whether or
        /// not a frame ran between the two calls.
        void keepBounce(bool bounce);

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

        /// The accumulator's and the wavelet's images: the three histories the accumulator reads and
        /// writes, the blend it hands the cascade, and the scratch the cascade ping-pongs through.
        /// Past the surface's two, empty where the bounce's images were let go (`keepBounce`).
        struct AccumulateImages
        {
            const Image& mColourBefore;

            /// The surface last frame's histories belong to, as every temporal pass that asks
            /// whether a texel is still the same surface reads it (`heldSurfaceMatches`): the normal
            /// and the distance the accumulator wrote for each pixel. **One surface history for
            /// every pass that asks**, since each asks it of the same pixels of the same frames; the
            /// accumulator writes it, and the shadow denoiser and the glossy filter only read it.
            const Image& mSurfaceBefore;
            const Image& mMomentsBefore;

            /// Written by the cascade's first level and read by the accumulator next frame — SVGF's
            /// feedback, so what carries forward is the filtered light.
            const Image& mColour;
            const Image& mSurface;
            const Image& mMoments;

            /// This frame's bounce blended with the history, which the cascade filters. One image
            /// and not a pair, because nothing reads it after the frame that wrote it.
            const Image& mBlended;

            /// The cascade's other half of the ping-pong.
            const Image& mScratch;

            /// The share of the bounce that is the fill, through the same four: last frame's mean,
            /// the mean the first level writes, the blend and the scratch.
            const Image& mFillBefore;
            const Image& mFill;
            const Image& mFillBlended;
            const Image& mFillScratch;

            /// The fast means of the bounce and the fill (`ACCUMULATE_FAST`), last frame's and this
            /// one's, and the accumulator's blend of them, which the clamp reads at a pixel's
            /// neighbours as it writes the pixel's mean: the accumulator's own, which the cascade
            /// never writes.
            const Image& mFastBefore;
            const Image& mFast;
            const Image& mFastBlended;

            bool mFresh;
        };

        /// The shadow denoiser's: **six images of a frame's own and two that carry over**, the
        /// SDK's arrangement and the penumbra beside it. The mask pass packs the rays' bits and the
        /// tiles' penumbra, the temporal pass writes its blend
        /// into the scratch, the first filter level writes the history the next frame's temporal pass
        /// reads, the second writes the scratch again — where a cleared tile keeps the temporal pass's
        /// exact value — and the third writes what the composite reads.
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

            bool mFresh;
        };

        /// The glossy filter's: the mean and its frame count, the last frame's and this one's.
        struct SpecularImages
        {
            const Image& mMeanBefore;
            const Image& mMean;

            /// `AccumulateImages::mSurfaceBefore`.
            const Image& mHeldSurface;

            bool mFresh;
        };

        /// The pane filter's: the mean with its frame count, and the layer it belongs to, the last
        /// frame's and this one's.
        struct PaneImages
        {
            const Image& mMeanBefore;
            const Image& mHeldBefore;
            const Image& mMean;
            const Image& mHeld;
            bool mFresh;
        };

        AccumulateImages accumulate(const TemporalTurns::Step& step) const;
        ShadowImages shadow(const TemporalTurns::Step& step) const;
        SpecularImages specular(const TemporalTurns::Step& step) const;
        PaneImages pane(const TemporalTurns::Step& step) const;

    private:
        const Device& mDevice;

        /// Makes the bounce's images at `mWidth` by `mHeight`.
        void makeBounce();

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// Empty until `resize`, and the bounce's own past `mSurface` while `keepBounce` lets them go.
        ImagePair mSurface;
        ImagePair mColour;
        ImagePair mMoments;
        Image mBlended;
        Image mScratch;
        ImagePair mFill;
        Image mFillBlended;
        Image mFillScratch;
        ImagePair mFast;
        Image mFastBlended;

        ImagePair mShadowMoments;
        Image mShadowHistory;
        Image mShadowScratch;
        Image mShadowVisibility;
        Image mShadowTiles;
        Image mShadowPenumbra;
        Image mShadowMask;

        ImagePair mSpecularMeans;

        ImagePair mPaneMeans;
        ImagePair mPaneHeld;

        /// Started again by `resize`, so the first frame after one reads no image nothing wrote.
        TemporalTurns mTurns;
    };
}
