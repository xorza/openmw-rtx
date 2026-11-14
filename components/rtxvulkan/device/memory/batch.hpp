#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <volk.h>

#include "buffer.hpp"
#include "image.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;

    /// Where a batch put an upload's bytes: the buffer holding them, and how far into it they
    /// start. See `Batch::stage`.
    struct StagingRun
    {
        VkBuffer mBuffer = VK_NULL_HANDLE;
        VkDeviceSize mOffset = 0;
    };

    /// Room a batch lent out of its staging: where it is, and the bytes to write there. See
    /// `Batch::reserve`.
    struct StagingLend
    {
        StagingRun mRun;
        std::span<std::byte> mBytes;
    };

    /// One run of a write that lands in pieces: where its bytes start in what they are written from,
    /// where they go in the buffer written, and how many. See `Batch::stageRuns`.
    struct CopyRun
    {
        VkDeviceSize mFrom = 0;
        VkDeviceSize mTo = 0;
        VkDeviceSize mSize = 0;

        bool operator==(const CopyRun& other) const = default;
    };

    /// What one batch holds until it ends: what callers handed over with `Batch::keep`, and the
    /// staging blocks it took. The pool's, and lent to each batch in turn (`CommandPool`).
    struct BatchHold
    {
        std::vector<Buffer> mBuffers;
        std::vector<Image> mImages;
        std::vector<std::size_t> mBlocks;

        /// `Batch::stageRuns`' regions, cleared and refilled for each call.
        std::vector<VkBufferCopy> mRegions;
    };

    /// One command buffer that a run of setup records into, submitted and waited on once. A load path's
    /// cost is round trips, not work: a cell arriving at Balmora creates 361 textures, and a submit
    /// each is 367 waits. The batch holds the staging blocks it took off the pool's ring, because the
    /// copy has not run when an upload returns, and gives them back stamped with the submit it rides
    /// when it ends — `keep` says what else it holds until then. What is recorded is readable by what
    /// is recorded after it (`uploadBuffer` ends in a barrier, a `Texture` leaves its image in
    /// `SHADER_READ_ONLY_OPTIMAL`) and nothing else here orders anything.
    class Batch
    {
    public:
        explicit Batch(CommandPool& pool);

        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        Batch(Batch&&) = delete;
        Batch& operator=(Batch&&) = delete;

        /// Throws away anything still recorded. A destructor is not where a submit belongs: it
        /// runs during unwinding too, and a constructor that fails half way leaves a recording
        /// naming resources its own members have already let go of. A caller that simply forgot
        /// `flush` or `defer` is the contract the assert names.
        ~Batch();

        /// What to record into. Opens a command buffer on first use, and again after a flush.
        VkCommandBuffer getCommands();

        /// The device the pool records for, which is the one everything staged through this is
        /// made on.
        const Device& getDevice() const;

        /// Holds what this recording reads, or what an earlier submit may still read, and buries it under
        /// the submit this batch rides when the batch ends, whichever way it ends: a buffer staged
        /// through, a build's scratch, an image the last frame drew with. Buried then and not now,
        /// because a burial is stamped with the next submit, which this batch's is not until handed over.
        void keep(Buffer&& buffer);
        void keep(Image&& image);

        /// Writes `bytes` into the batch's staging and says where they landed. One block serves every
        /// upload of a batch, where a buffer apiece was three driver calls per upload. Appended and never
        /// rewound, because nothing has run yet.
        StagingRun stage(std::span<const std::byte> bytes);

        /// The same room, `bytes` long, lent empty for a caller that produces the bytes where they
        /// land — a picture the interface fills through `lock`, which then crosses main memory
        /// once. Writable until the batch ends; write it and do not read it back, because it is
        /// write-combined memory.
        StagingLend reserve(VkDeviceSize bytes);

        /// Stages `runs` of `source`, packed one after another, and copies each into `into` where
        /// it says, as one copy command with a region a run: a table's rows owed apiece are hundreds
        /// of runs a frame, and a command a run was hundreds of commands. A run of no bytes is no
        /// region. Nothing is ordered here, as `stageInto` orders nothing.
        void stageRuns(const Buffer& into, std::span<const std::byte> source, std::span<const CopyRun> runs);

        /// Buries what it held under the submit it is about to make, submits what has been
        /// recorded and waits for it. A batch nobody used costs nothing.
        void flush();

        /// Hands what has been recorded to the pool, to go ahead of the pool's next submit, and
        /// buries what it held under that submit; records nothing more. The other way out of a
        /// batch, for a load that is followed by a submit anyway.
        void defer();

        /// Gives back what was begun, submitting nothing, and buries what the batch held under the
        /// next submit: the third way out, for a caller whose recording came to nothing.
        void abandon();

    private:
        /// Buries everything this batch was holding and gives its staging back, whichever way it
        /// ended, under the next submit — which is the one this batch rides where it was handed
        /// over, and one after every reader where it was flushed or thrown away.
        void release();

        CommandPool& mPool;
        VkCommandBuffer mCommands = VK_NULL_HANDLE;

        /// The pool's `BatchHold` this batch has, and how much of its last block is spoken for.
        /// See `stage`.
        std::size_t mHold;
        VkDeviceSize mFilled = 0;
    };

    /// Stages `bytes` through the batch's own staging and copies them into `into` at `offset`.
    /// Nothing is ordered here: a run of these is made readable together by `orderStagedWrites`.
    void stageInto(Batch& batch, const Buffer& into, VkDeviceSize offset, std::span<const std::byte> bytes);

    /// A device-local buffer holding `bytes`, staged through the batch's staging. The copy is
    /// recorded into `batch` and ends in a barrier, so a structure can be built from it in the same
    /// batch.
    Buffer uploadBuffer(
        Batch& batch, std::span<const std::byte> bytes, VkBufferUsageFlags usage, std::string_view name);

    /// Makes every staged write recorded into `batch` visible to whatever reads or writes it next —
    /// one dependency for a run of writes that are read together, rather than one barrier per
    /// buffer. A write too: the skin pass writes the posed normals over the bind normals staged into
    /// them.
    void orderStagedWrites(Batch& batch);

    /// Copies `bytes` into `image` by `regions`, and leaves it where a sampler expects it. From
    /// `UNDEFINED`, so whatever the image held is thrown away; an image written over in part —
    /// the interface's textures — transitions from where it stands instead, in `GuiTextures`.
    /// `regions` is written to: each is moved along by where the bytes landed in the staging.
    void uploadImage(
        Batch& batch, Image& image, std::span<const std::byte> bytes, std::span<VkBufferImageCopy> regions);

    template <class T>
    Buffer uploadBuffer(Batch& batch, std::span<const T> data, VkBufferUsageFlags usage, std::string_view name)
    {
        return uploadBuffer(batch, std::as_bytes(data), usage, name);
    }
}
