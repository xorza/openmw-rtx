#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/trace/denoise/denoisehistory.hpp>

#include "fogvolume.hpp"
#include "gbuffer.hpp"
#include "spritebin.hpp"
#include "tracepast.hpp"

namespace Rtx
{
    class Device;
    struct TracePasses;
    struct TraceRecording;
    struct TraceResult;
    struct VisibilityInputs;

    /// Everything one camera's trace writes, at one extent — one chain however many cameras have
    /// one, so a barrier cannot go missing from a second copy. What differs between two of these
    /// is what the caller hands in: the extent, and whether a trace fills it or a corner of it.
    /// What becomes of a finished picture — the frame's presented pair, a picture's byte
    /// target — is not here.
    class TraceChain
    {
    public:
        /// Nothing has an extent until `resize` is called. Its images are the frame's targets
        /// (`MemoryUse::Frame`), made again only once the device drained what read the last.
        ///
        /// @param passes what the chain traces with, which outlives it.
        /// @param bins how many sprite bins the chain keeps, `VisibilityInputs::mTraceSlot` picking
        ///        one: one per frame in flight for the world's, and one for the pictures'. One is
        ///        enough there though nothing waits for a picture, because the pictures' batches
        ///        stand in queue order and a bin's tables are written on the device alone
        ///        (`SpriteBin`).
        /// @param radiance how wide the radiance channels and the frame composed from them are
        ///        stored — the run's choice, which `Rtx::RadianceWidth` argues.
        /// @param past whether a trace keeps what it leaves for the next: the world's chain does, and
        ///        the pictures', each traced with its past lost, does not.
        TraceChain(const Device& device, const TracePasses& passes, std::uint32_t bins, RadianceWidth radiance,
            TracePast past);

        /// What a chain at this extent takes of the device's memory, the running sum apart, and the
        /// images its arena holds apart.
        static VkDeviceSize bytesAt(
            const Device& device, std::uint32_t width, std::uint32_t height, RadianceWidth radiance, TracePast past);

        /// The images a chain at this extent uses inside one stretch of a frame, which an arena places
        /// (`ImageArena`): the trace's stage images, the denoiser's scratch, then the canvas the
        /// display lays the puffs over, at `radiance`'s format, appended to `into`. `resize` takes
        /// them in this order.
        static void declareShared(
            std::uint32_t width, std::uint32_t height, RadianceWidth radiance, std::vector<ArenaImage>& into);

        /// How many images `declareShared` declares.
        static std::size_t getSharedCount();

        /// What the running sum at this extent takes, which only a trace that averages makes.
        static VkDeviceSize sumBytesAt(const Device& device, std::uint32_t width, std::uint32_t height);

        /// Builds the chain at exactly this extent, whatever it was before, and nothing where it
        /// already stands at it, with the images `declareShared` declared at it as `shared` placed
        /// them: taken again whether the extent moved or not, since an arena is made again whenever
        /// anything it holds moves.
        void resize(std::uint32_t width, std::uint32_t height, const ArenaRun& shared);

        /// Lets every image go, until the next `resize`.
        void release();

        /// The extent the images are at, which is what a dispatch over the whole of one covers.
        /// Nought until the first `resize`.
        std::uint32_t getWidth() const { return mWidth; }
        std::uint32_t getHeight() const { return mHeight; }

        RadianceWidth getRadiance() const { return mRadiance; }

        /// Whether the images exist, which is the same question as whether the extent is set.
        bool isBuilt() const { return mChannels != nullptr; }

        /// Whether a picture this big fits what is built: a smaller picture uses a corner of a larger
        /// one's images.
        bool holds(std::uint32_t width, std::uint32_t height) const
        {
            return isBuilt() && width <= mWidth && height <= mHeight;
        }

        /// The frame: one picture in linear radiance, before anything upscales it and before the
        /// display curve, in its own order, which the channels are not (`storedOf`): what the
        /// composite or the cascade's last level wrote.
        const Image& getColour() const { return mComposed; }

        /// What the trace writes and the composite reads: one picture's light, still in pieces.
        const GBuffer& getChannels() const { return *mChannels; }

        /// What the last `record`'s denoisers wrote, null where a filter did not run on it
        /// (`DenoiseHistory::written`).
        void denoiserWritten(std::array<const Image*, sDenoiserImageCount>& into) const { mDenoise.written(into); }

        /// Records one camera's whole trace, from the discards it opens with to the barrier after
        /// the composite, and hands back what the display reads of it. What the caller keeps is
        /// what a frame has and a picture has not — the frame ring, the upscaler, the lens, the
        /// measured exposure and the display curve.
        TraceResult record(VkCommandBuffer commands, const TraceRecording& what);

        /// Lets go of the running total, which a new scene or a new size has no use for: a sum over
        /// one scene means nothing over the next. The first frame that averages makes another.
        void dropSum() { mSum = Image(); }

    private:
        const Device& mDevice;
        const TracePasses& mPasses;

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        const RadianceWidth mRadiance;
        const TracePast mPast;

        std::unique_ptr<GBuffer> mChannels;
        std::unique_ptr<FogVolume> mFogVolume;

        /// The frame in its own order, `getColour`.
        Image mComposed;

        /// What the display lays the puffs over in place of `mComposed` (`TraceResult::mCanvas`),
        /// one image of the chain's arena.
        ArenaRun mCanvas;

        /// The sprite bins — `VisibilityInputs::mTraceSlot` picks — so the frame behind keeps the
        /// tables its trace reads while this frame's bin writes its own. Made once, at the count the
        /// chain was made with.
        std::vector<SpriteBin> mBins;

        /// What the shared denoising passes keep of this camera, at the extent.
        DenoiseHistory mDenoise;

        /// The running sum a reference is built out of, empty until a trace averages. Not a history
        /// and nothing here reprojects: a plain per-pixel total over however many frames the caller
        /// asked to average. Whether it exists is also whether anything has written it, because the
        /// trace that makes one fills it.
        Image mSum;
    };
}
