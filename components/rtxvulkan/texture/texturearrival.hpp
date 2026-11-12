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
    class GpuTimer;
    struct TexturePasses;

    /// How many bytes of means the spreads of one group are carried through at once, past what one
    /// map needs alone (`TextureArrival::recordSpreads`): a storage block's worth, two normal maps of
    /// two thousand texels a side.
    inline constexpr VkDeviceSize sSpreadMeansRoom = 64 * 1024 * 1024;

    /// What a run of arriving textures leaves the device to do, recorded once for the whole run a phase at a time:
    /// every copy, then each level of every spread and every bake, with one barrier between two phases rather than
    /// several around each texture. A barrier drains the queue, and a texture's dispatches are a few groups each:
    /// recorded texture by texture, a cell of three hundred textures was thousands of barriers around idle work.
    ///
    /// Work names its images by address, so an image stays where it is from the call that names it to `record`. Kept
    /// and refilled per run, so what it holds settles at the busiest run so far.
    class TextureArrival
    {
    public:
        /// @param meansRoom what one group of spreads works in: `sSpreadMeansRoom`, and less for a
        ///        test that splits a few maps into groups.
        explicit TextureArrival(const Device& device, VkDeviceSize meansRoom = sSpreadMeansRoom);

        TextureArrival(const TextureArrival&) = delete;
        TextureArrival& operator=(const TextureArrival&) = delete;

        /// Copies `staged`, which the caller staged through the batch the run is recorded into, into
        /// `image` by `regions`, whose offsets are into `staged`. `image` is met undefined.
        void upload(const StagingRun& staged, const Image& image, std::span<const VkBufferImageCopy> regions);

        /// `NormalSpreadPass` of `map` into `spread`, met undefined, through the run's means.
        void spread(const Image& map, const Image& spread);

        /// `SpriteLightPass` of `source` into `bake`, met undefined.
        void bake(const Image& source, const Image& bake);

        /// Records the run into `batch` and empties it, a phase that has work timed in its own zone
        /// where `timer` is given. Every image the run names is left as a texture the trace samples.
        void record(Batch& batch, const TexturePasses& passes, GpuTimer* timer);

        /// How many bytes of means the spreads are carried through now, which an arrival that needs
        /// more is priced the rest of (`TextureArray::costAt`).
        VkDeviceSize getMeansBytes() const { return mMeans.get().getSize(); }

        /// The bytes of means an arrival needs whose spreads come to `total` together, the largest of
        /// them `largest`: what its largest group holds, the room or the one map past it, and never
        /// more than all of them.
        VkDeviceSize meansNeeded(VkDeviceSize total, VkDeviceSize largest) const;

        // Read by the tests and by nothing else.
        /// How many barrier commands the last `record` emitted.
        std::size_t getRecordedBarriers() const { return mRecordedBarriers; }
        const Buffer& getMeans() const { return mMeans.get(); }

    private:
        struct Upload
        {
            const Image* mImage;
            StagingRun mStaged;
            std::uint32_t mFirst;
            std::uint32_t mCount;
        };

        struct Spread
        {
            const Image* mMap;
            const Image* mSpread;
        };

        struct Bake
        {
            const Image* mSource;
            const Image* mBake;
        };

        /// The copies, and every image the run names out of `UNDEFINED`.
        void recordWrites(VkCommandBuffer commands, Barriers& barriers);

        /// Each level of every spread of a group, a level of all of them between two barriers, one
        /// group after another over the same means.
        void recordSpreads(VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes, GpuTimer* timer);

        void recordBakes(VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes, GpuTimer* timer);

        /// The copies' regions, every upload's in one run, moved along by where its bytes landed.
        std::vector<VkBufferImageCopy> mRegions;

        std::vector<Upload> mUploads;
        std::vector<Spread> mSpreads;
        std::vector<Bake> mBakes;

        /// The room every barrier of a run is recorded in, sized to its widest.
        std::vector<VkImageMemoryBarrier2> mImageBarriers;

        /// The means the spreads are carried through, one group at a time: grown to the largest group so far and worked
        /// in again by every group after. A room and not a chain apiece, because a map's float means are four times
        /// what the map keeps, and a chain of them for every normal map of an arrival at once held every texture to a
        /// smaller side for good, for memory let go of a frame later.
        const VkDeviceSize mMeansRoom;
        GrowableBuffer mMeans;

        /// Where each group of `recordSpreads` ends in the spreads, the last at their end.
        std::vector<std::size_t> mGroupEnds;

        std::size_t mRecordedBarriers = 0;
    };
}
