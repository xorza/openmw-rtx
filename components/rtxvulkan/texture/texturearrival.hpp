#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    class Barriers;
    class Device;
    struct TexturePasses;

    /// What a run of arriving textures leaves the device to do, recorded once for the whole run a
    /// phase at a time: every copy and clear, then each level of every chain, every shading map,
    /// each level of every spread and every bake, with one barrier between two phases rather than
    /// several around each texture. A barrier drains the queue, and a texture's dispatches are a
    /// few groups each: recorded texture by texture, a cell of three hundred textures was
    /// thousands of barriers around work that left the device idle between them.
    ///
    /// Work names its images by address, so an image stays where it is from the call that names it
    /// to `record`: a `Texture` stands where its owner keeps it, and what the run holds for its
    /// batch alone is held here, in room `open` makes. Kept and refilled per run, so what it holds
    /// settles at the busiest run so far.
    class TextureArrival
    {
    public:
        explicit TextureArrival(const Device& device);

        TextureArrival(const TextureArrival&) = delete;
        TextureArrival& operator=(const TextureArrival&) = delete;

        /// Begins a run of at most `textures`, with room for every image it holds for its batch.
        void open(std::size_t textures);

        /// Stages `bytes` through `batch` and copies them into `image` by `regions`, whose offsets
        /// are into `bytes`. `image` is met undefined.
        void upload(Batch& batch, const Image& image, std::span<const std::byte> bytes,
            std::span<const VkBufferImageCopy> regions);

        /// Holds `image` until the run is recorded, and then hands it to the batch: an upload a
        /// chain is made from, or the means a spread is made through. Where it stays until then.
        const Image& hold(Image&& image);

        /// Clears `map`, met undefined, to the neutral shading factor.
        void clearNeutral(const Image& map);

        /// `MipChainPass` from `source`, uploaded in this run, into `chain`, met undefined.
        void chain(const Image& source, const Image& chain, bool encoded);

        /// `ShadingPass` of `source` into `map`, met undefined.
        void shade(const Image& source, const Image& map, bool punchThrough);

        /// `NormalSpreadPass` of `map` into `spread` through `means`, both met undefined.
        void spread(const Image& map, const Image& means, const Image& spread);

        /// `SpriteLightPass` of `source` into `bake`, met undefined.
        void bake(const Image& source, const Image& bake);

        /// Records the run into `batch` and empties it, handing what it held to the batch. Every
        /// image the run names is left as a texture the trace samples.
        void record(Batch& batch, const TexturePasses& passes);

        // Read by the tests and by nothing else.
        /// How many barrier commands the last `record` emitted.
        std::size_t getRecordedBarriers() const { return mRecordedBarriers; }

    private:
        struct Upload
        {
            const Image* mImage;
            StagingRun mStaged;
            std::uint32_t mFirst;
            std::uint32_t mCount;
        };

        struct Chain
        {
            const Image* mSource;
            const Image* mChain;
            bool mEncoded;
        };

        struct Shade
        {
            const Image* mSource;
            const Image* mMap;
            bool mPunchThrough;
        };

        struct Spread
        {
            const Image* mMap;
            const Image* mMeans;
            const Image* mSpread;
        };

        struct Bake
        {
            const Image* mSource;
            const Image* mBake;
        };

        /// The copies and clears, and every image the run names out of `UNDEFINED`.
        void recordWrites(VkCommandBuffer commands, Barriers& barriers);

        /// Each level of every chain, a level of all of them between two barriers.
        void recordChains(VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes);

        /// Every sum, one barrier, every map.
        void recordShading(VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes);

        /// Each level of every spread, as the chains are.
        void recordSpreads(VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes);

        void recordBakes(VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes);

        /// The copies' regions, every upload's in one run, moved along by where its bytes landed.
        std::vector<VkBufferImageCopy> mRegions;

        std::vector<Upload> mUploads;
        std::vector<const Image*> mClears;
        std::vector<Chain> mChains;
        std::vector<Shade> mShades;
        std::vector<Spread> mSpreads;
        std::vector<Bake> mBakes;

        /// Reserved by `open` and never grown past it, so what work names here does not move.
        std::vector<Image> mHeld;

        /// The room every barrier of a run is recorded in, sized to its widest.
        std::vector<VkImageMemoryBarrier2> mImageBarriers;

        /// Every texture's own sums, `ShadingPass::sSumBytes` each, so a run's sums are dispatched
        /// together: one buffer for every texture in turn ordered each against the one before.
        GrowableBuffer mSums;

        std::size_t mRecordedBarriers = 0;
    };
}
