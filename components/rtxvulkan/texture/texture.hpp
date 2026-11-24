#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <osg/Vec4f>
#include <vulkan/vulkan_core.h>

#include <components/misc/result.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/image/imageshape.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/renderer/residencydevice.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/descriptorsets.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/slottable.hpp>
#include <components/rtxvulkan/device/readstamp.hpp>

#include "texturearrival.hpp"

namespace Rtx
{
    class Batch;
    class Device;
    class Uploads;
    class GpuTimer;
    class GroundCompositePass;
    class ContentArena;
    struct TexturePasses;

    /// **Where a texture's images stand**: at an offset of the content arena's memory, the slot residency reserved for
    /// it, or, with no memory named, memory of their own that nothing stands in for (`MemoryUse::Essential`).
    struct TexturePlace
    {
        const DeviceMemory* mMemory = nullptr;
        VkDeviceSize mOffset = 0;
    };

    /// A sampled image on the GPU, the levels a content file brought for it, and what was measured on it — two
    /// `Image`s: the first uploaded, the second its companion, made off the first on the device as it arrives
    /// (`TextureCompanion`): for a normal map, the spread its levels lost, and none for anything else. The companion
    /// travels with the texture because it is measured on it and read at its coordinates, and stands after it in its
    /// slot.
    class Texture
    {
    public:
        /// A slot with nothing in it yet, which is what the array holds while it is being filled.
        Texture() = default;

        /// What an image of `shape` is made as, and its companion, nothing where none stands beside it: the one
        /// statement a price and a stand both read.
        static ImageDescription describe(const ImageShape& shape);
        static std::optional<ImageDescription> describeCompanion(const ImageShape& shape);

        /// What a texture of `shape` takes of a slot, its companion after its image at the companion's alignment;
        /// nothing where the device takes no image of it.
        static std::optional<ContentPrice> price(const Device& device, const ImageShape& shape);

        /// The shape a file stands as from level `first`, and what the image a texture stands holds.
        static ImageShape shapeOf(const TextureData& data, std::uint32_t first);
        ImageShape getShape() const { return mShape; }

        /// Stands a texture from its file's bytes in this one, which is empty, from level `first` on, uploaded level by
        /// level at `place`. What makes the texture is `arrival`'s to record, and this stays where it is until then:
        /// the work names its images.
        ///
        /// @param name what a capture calls it. Empty where the build names no objects.
        /// @param regions the caller's scratch, cleared and refilled here with one copy per level.
        void standFile(const Device& device, Batch& batch, TextureArrival& arrival, const TextureData& data,
            std::uint32_t first, std::string_view name, std::vector<VkBufferImageCopy>& regions,
            const TexturePlace& place);

        /// Stands `from` in this one, which is empty, `dropped` levels down at `place`: its levels past the first
        /// `dropped` copied on the device, nothing read from the host, and a normal map's spread measured again. A
        /// move out of a draining slab drops none. `from` stays where it is until `arrival` records.
        void standMovedDown(const Device& device, TextureArrival& arrival, const Texture& from, std::uint32_t dropped,
            std::string_view name, const TexturePlace& place);

        /// Stands a sprite's light bake in this one, which is empty, at `place`: shaped like `source`, made from its
        /// alpha. `source` stands, uploaded in an earlier batch or in `arrival`'s run, which records every copy ahead
        /// of every bake.
        ///
        /// @param format what the description says the bake is, which is its image's format.
        void standBake(const Device& device, TextureArrival& arrival, const Texture& source, TextureFormat format,
            std::string_view name, const TexturePlace& place);

        /// Stands a chunk's flattened ground in this one, which is empty, at `place`: `GROUND_COMPOSITE_EXTENT` square
        /// in BC7, with a chain to one texel, left undefined. Copied into by `TextureArray::bakeComposites`, in the
        /// placement after it arrives.
        void standComposite(
            const Device& device, TextureFormat format, std::string_view name, const TexturePlace& place);

        /// Stands one texel of `colour` in this one, which is empty, whole floats so the value is the one named, under
        /// the neutral map: what `TEXTURE_NEUTRAL` stands, once, when the array is made. Essential.
        void standColour(const Device& device, Batch& batch, TextureArrival& arrival, std::string_view name,
            const osg::Vec4f& colour);

        Texture(Texture&&) noexcept = default;
        Texture& operator=(Texture&&) noexcept = default;
        ~Texture() = default;

        /// Whether the slot holds no texture.
        bool isEmpty() const { return mImage.isEmpty(); }

        /// The texture and its companion as a sampled image takes them, with no sampler, from the read-only layout an
        /// upload leaves them in: a read pairs them with the sampler the slot's wrap names (`TEXTURE_BIND_SAMPLERS`).
        VkDescriptorImageInfo describe() const
        {
            return mImage.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        /// Whether something is measured beside the texture: a normal map's spread.
        bool hasCompanion() const { return !mCompanion.isEmpty(); }

        VkDescriptorImageInfo describeCompanion() const
        {
            return mCompanion.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Read by the tests, the interface's pass, which draws with one, and the array, which hands a composite's image
        // to the bake.
        VkImageView getView() const { return mImage.getView(); }
        const Image& getImage() const { return mImage; }

        /// Its companion, empty where nothing is measured beside it.
        const Image& getCompanion() const { return mCompanion; }

        /// How the file said this is addressed past its edges, which picks the sampler a read of it goes through
        /// (`TEXTURE_WRAP_SHIFT`).
        TextureWrap getWrap() const { return mWrap; }

        /// What the texture keeps on the device, its companion included: `TextureCost::standing` of what it was made
        /// as.
        VkDeviceSize getBytes() const { return mBytes; }

    private:
        /// Makes the image and the companion of `shape` at `place`.
        void make(const Device& device, const ImageShape& shape, std::string_view name, const TexturePlace& place);

        Image mImage;
        Image mCompanion;
        ImageShape mShape;

        TextureWrap mWrap = TextureWrap::Repeat;
        VkDeviceSize mBytes = 0;
    };

    /// What a texture array stands: how many of its slots hold a texture, and what those come to, kept as each slot
    /// changes, so neither is a walk of the array. A slot that draws the stand-in holds none.
    struct TexturesHeld
    {
        std::uint32_t mCount = 0;
        VkDeviceSize mBytes = 0;

        /// `mBytes` by what stands in each slot.
        TextureSourceBytes mBySource;

        /// The canvases the scene's composites are baked on and the blocks they are encoded through, beside the slots
        /// (`priceCanvas`, `priceBlocks`): nought until the first composite stands.
        VkDeviceSize mCanvasBytes = 0;
    };

    /// Every texture a scene uses, in one descriptor array a shader indexes by material, and every texture's companion
    /// in a second array beside it at the same slot (an array and not a buffer, because each is a grid the texture unit
    /// filters; see `texturearray.glsl`).
    ///
    /// **It stands an image in the slot residency hands it and decides nothing** (`StandImage`, `DropImage`): which
    /// texture stands at which level, and where, is `Residency`'s. A slot that stands nothing yet draws the stand-in.
    ///
    /// One set per frame in flight, and a debt per set, the way `SlotTable` keeps its copies: an operation writes the
    /// slots it touched into the set the next placement binds and owes them to the other, which is paid when that
    /// set's frame comes round.
    class TextureArray
    {
    public:
        /// An array of `slots` textures, every one drawing nothing until the table names it.
        ///
        /// @param layout what `describeLayout` made: every array is shaped by the one the renderer keeps.
        /// @param passes the renderer's, which the stand-in and the neutral texel are made with.
        /// @param arrival the renderer's, which every array's arrivals are recorded through.
        /// @param anisotropy `RenderProfile::mAnisotropy`: what the footprint binding filters by.
        TextureArray(const Device& device, Batch& batch, const SetLayout& layout, const TexturePasses& passes,
            TextureArrival& arrival, std::uint32_t slots, std::uint32_t anisotropy = 1);

        /// The shape of every set an array here holds: three bindless arrays, partially bound and updated after bind.
        static SetLayout describeLayout(const Device& device);

        /// What one `execute` is of: the batch and the arrival it records into, the arena whose memory the slots
        /// stand in, and where the work is timed, or null.
        struct Executing
        {
            Batch& mBatch;
            const TexturePasses& mPasses;
            TextureArrival& mArrival;
            const ContentArena& mArena;
            GpuTimer* mTimer = nullptr;
        };

        /// **Does residency's texture operations in order**: each `StandImage` stands its texture in the slot its place
        /// names — a file's levels uploaded, a lowering or a move copied on the device, a bake made from its sources —
        /// and each `DropImage` lets its texture go. What a slot held buries itself under the frames that may read it.
        /// The descriptors are owed to every set and written by `sync`.
        void execute(const Executing& executing, std::span<const ResidencyOp> ops);

        /// A slot the scene took draws the stand-in until something stands in it; one it freed draws nothing.
        void standIn(std::span<const TextureIndex> taken);
        void free(std::span<const TextureIndex> freed);

        /// Writes the descriptors `slot`'s set owes, and the texel words its copy owes through `uploads`. Before the
        /// placement that binds it, after `finishReads`.
        void sync(FrameSlot slot, Uploads& uploads);

        /// Records the bake of every composite that arrived since the last call, into the composites themselves,
        /// reading `slot`'s set and texel counts and the tables `tables` names. After `sync(slot)`. True where a bake
        /// was recorded. Timed as `FrameZone::Ground` on `timer`, null where nothing is timed.
        bool bakeComposites(VkCommandBuffer commands, const GroundCompositePass& pass, FrameSlot slot,
            const Shaders::GpuTables& tables, GpuTimer* timer);

        /// Waits until nothing on the queue binds `slot`'s set, ahead of the `sync` that writes it.
        void finishReads(FrameSlot slot) const;

        /// Filters the footprint binding by `anisotropy`, written into every set before this returns.
        void setAnisotropy(std::uint32_t anisotropy);

        /// The set `slot`'s frame binds, which `sync(slot)` brought up to date.
        VkDescriptorSet getSet(FrameSlot slot) const;

        /// How long the array is: not how many textures there are (`getHeld`).
        std::uint32_t getCount() const { return static_cast<std::uint32_t>(mSlots.size()); }

        /// Where `slot`'s copy of the texel counts is — `GpuTables::mTextureTexels`.
        VkDeviceAddress getTexelsAddress(FrameSlot slot) const { return mTexels.addressFor(slot); }

        /// `slot`'s word in those counts: how many texels stand in it, with `TEXTURE_STANDS_IN` over the count where
        /// they are the stand-in's.
        std::uint32_t getTexels(const TextureIndex slot) const { return mTexels.getRows()[slot]; }

        /// How many texels across and down stand in `slot` — the stand-in's where it draws the stand-in, and nought
        /// where it holds nothing.
        Shaders::uvec2 getExtent(TextureIndex slot) const;

        /// The image `slot` samples, the stand-in's where it draws the stand-in: read back by the tests.
        const Image& getImage(TextureIndex slot) const;

        /// What the array actually stands.
        const TexturesHeld& getHeld() const { return mHeld; }

        /// The most texels a side the device takes of every image a texture is made as.
        std::uint32_t getSideLimit() const { return mSideLimit; }

    private:
        /// One slot of the array: the texture standing in it, or the stand-in, which stands once beside the array for
        /// every slot that draws it.
        struct Slot
        {
            /// Its own object, so the work an arrival records names it wherever the slot moves as the array grows.
            std::unique_ptr<Texture> mTexture;
            bool mStandIn = false;

            /// What the texture was made from, which `mHeld` counts its bytes under.
            TextureSource mSource = TextureSource::File;

            bool isEmpty() const { return mTexture == nullptr && !mStandIn; }
        };

        /// Stands one operation's texture in its slot.
        void stand(const Executing& executing, const StandImage& stand);

        /// Lets what `slot` stands go, kept until the arrival records, since its work may copy out of it.
        void replace(Slot& slot);

        /// Writes slot `at`'s texel word into every copy's owed row and owes its descriptors to every set.
        void owe(TextureIndex at);

        /// Takes what `slot` stands off `mHeld`, before it changes, and counts what it stands after.
        void forget(const Slot& slot);
        void count(const Slot& slot);

        /// Where `slot`'s bytes are counted in `held`.
        static std::uint64_t& bytesBySource(TexturesHeld& held, const Slot& slot);

        /// What `slot` is sampled through: its texture, or the stand-in.
        const Texture& standingIn(const Slot& slot) const
        {
            return slot.mStandIn || slot.mTexture == nullptr ? mStandIn : *slot.mTexture;
        }

        /// Queues a write of `image` into `set` at `binding[element]`, behind the image info the write names by
        /// address.
        static void queueWrite(VkDescriptorSet set, std::uint32_t binding, std::uint32_t element, VkDescriptorType type,
            const VkDescriptorImageInfo& image, std::vector<VkDescriptorImageInfo>& images,
            std::vector<VkWriteDescriptorSet>& writes);

        /// Grows the array to reach `slot`, and refuses one past what the binding holds.
        void reserveSlot(TextureIndex slot);

        const Device& mDevice;

        /// Cleared and refilled by every write, never freed.
        std::vector<VkDescriptorImageInfo> mImageScratch;
        std::vector<VkWriteDescriptorSet> mWriteScratch;
        std::vector<VkBufferImageCopy> mRegionScratch;

        /// What an operation took the place of, kept until its arrival records: an image buries itself as it goes,
        /// under the next submit.
        std::vector<std::unique_ptr<Texture>> mReplaced;

        /// Indexed by slot.
        RowVector<Slot, TextureIndex> mSlots;

        /// What the slots stand, kept by `forget` and `count` around every change to one.
        TexturesHeld mHeld;

        /// Where a chunk is baked before it is encoded into its slot, and the blocks each is encoded through: each made
        /// at the scene's first composite that needs it, and kept.
        struct CompositeCanvas
        {
            std::optional<Image> mAlbedo;
            std::optional<Image> mGloss;
            std::optional<Buffer> mBlocks;
        };
        CompositeCanvas mCanvas;

        /// Writes both sampler bindings into every set: at the making, and at a change of anisotropy.
        void writeSamplers();

        /// Makes the blocks and the canvas a composite of `source` is baked on, where either is missing.
        void makeCanvas(TextureSource source);

        std::array<Sampler, sTextureWrapCount> mSamplers;
        std::array<Sampler, sTextureWrapCount> mFootprintSamplers;

        /// The one texel every material with no diffuse names, at `TEXTURE_NEUTRAL` of every set.
        Texture mNeutral;

        /// `describeStandIn`, stood once, essential: what every slot that stands nothing yet draws.
        Texture mStandIn;

        std::uint32_t mSideLimit = 0;

        /// One word per slot of the array, `TEXTURE_SLOTS` long: how many texels the texture in it holds, which
        /// `coneLod` reads, and `TEXTURE_STANDS_IN` over it where they are the stand-in's. A copy per frame in flight.
        SlotTable<std::uint32_t, TextureIndex> mTexels;

        /// One set per frame in flight, each image array at the maximum the layout declares.
        DescriptorSets mSets;

        /// The last submit that bound each set.
        PerSlot<ReadStamp> mBound;

        /// The slots each set has yet to be told, each once however often it was written.
        PerSlot<SlotSet<TextureIndex>> mOwed;

        /// Which composites arrived and stand empty, whose ground each is and which of its two images: what the next
        /// placement bakes. Cleared by `bakeComposites` and never freed.
        struct PendingComposite
        {
            TextureIndex mSlot;
            MaterialIndex mMaterial;

            /// `GROUND_COMPOSITE_ALBEDO` or `GROUND_COMPOSITE_GLOSS`.
            std::uint32_t mOutput = 0;

            /// Where the layers it reads stand in `mPendingReads` (`TextureData::mReads`).
            std::size_t mReadsAt = 0;
            std::size_t mReadsCount = 0;
        };
        std::vector<PendingComposite> mPendingComposites;
        std::vector<TextureIndex> mPendingReads;

        /// Says, in the log and in a debug build's assert, every layer of `pending` that draws the stand-in as its bake
        /// is recorded.
        void checkReads(const PendingComposite& pending) const;
    };
}
