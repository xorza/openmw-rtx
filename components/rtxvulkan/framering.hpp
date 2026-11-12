#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/stepped.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/frame/digestpass.hpp>

namespace Rtx
{
    class Device;
    class Image;

    /// Where a frame's slot stands between one use and the next: nothing recorded, begun by a
    /// placement or a trace and not yet submitted, or submitted and not yet waited for. One
    /// step and not two flags, so a frame submitted twice or never begun is a call out of its
    /// turn and not a wait on a value nothing signals.
    enum class FrameState
    {
        Idle,
        Begun,
        Submitted,
    };

    struct FrameRecord
    {
        /// @param reporting whether its zones write timestamps and it may carry a digest
        ///        (`RendererOptions::mReporting`).
        FrameRecord(const Device& device, Reporting reporting);

        /// The placements' commands and the trace's, submitted apart because a picture inside
        /// the interface is traced between the two. Only the trace's value is waited on: it is
        /// later on the queue, so its signal covers every placement before it. One buffer per
        /// placement, because two placements sharing one would record over a submit in flight.
        LentCommands mPlaceCommands;
        std::size_t mPlacements = 0;
        LentCommands mTraceCommands;

        /// What the trace's submit signalled on the device's timeline, which is what says every
        /// placement of this frame and the trace have run.
        std::uint64_t mSubmitted = 0;

        Stepped<FrameState> mState{ FrameState::Idle };

        /// Whether the trace closed the frame, or `FrameRing::skip` did with none. An untraced
        /// frame is waited for like any other and comes back with no report: it has no counts, no
        /// picture and nothing a run could count against a traced frame.
        bool mTraced = false;

        /// `FrameResult::mInFlight`, taken at the submit.
        std::uint32_t mInFlight = 0;

        /// Its own timer and its own counts, because both are read after the wait, when the
        /// next frame is already writing its own. The counts are `Shaders::FrameCounts`.
        GpuTimer mTimer;
        Buffer mCounts;

        /// Where the frame copies the device's census as it ends (`NotFiniteCensus::record`), read
        /// after the wait as the counts are. Empty where the device does not count.
        Buffer mNotFinite;

        /// How many primary rays the trace cast, where the frame counts them, and nought where it
        /// does not: the device sums the misses, and the hits the report carries are the rays less
        /// those.
        std::uint32_t mCountedRays = 0;

        Reconstruction mReconstruction;

        /// The debug lines' vertices, rewritten every frame that draws any and grown to the busiest
        /// frame so far, in the frame's own commands: the slot is the frame's, so a write lands
        /// under no submit in flight.
        GrowableBuffer mDebugVertices;

        /// How much of `FrameRing::pictureOf` this frame wrote where `FrameRing::readPicture`
        /// recorded, nought for a frame that did not.
        VkDeviceSize mReadBackBytes = 0;

        /// Where `DigestPass` copies the frame's words, `DigestPass::sBytes` of them, and the digest
        /// the report carries: what `FrameRing::readDigest` was told while the frame was recorded,
        /// the words once it is waited for, and nothing for a frame that did not ask.
        Buffer mDigestLanes;
        std::optional<FrameDigest> mDigest;
    };

    /// The frames in flight, and the discipline that keeps them apart: two slots, and the CPU
    /// works one ahead of the GPU. Frame N+1 is walked and placed while frame N is traced; the
    /// frame after next takes N's slot and waits for it first. A report belongs to its frame
    /// and queues here when making room finished it, or a caller asking once a frame would be
    /// answered for fewer than half of them. What a frame reads back (counts, picture, digest) is
    /// cleared, recorded, ordered and read here, so no two places disagree about whether it does.
    class FrameRing
    {
    public:
        /// @param reporting whether a frame's zones write timestamps, and whether a frame may digest
        ///        what it traced, `readDigest`, which the digest's pass is made for
        ///        (`RendererOptions::mReporting`).
        FrameRing(const Device& device, Reporting reporting);

        FrameRing(const FrameRing&) = delete;
        FrameRing& operator=(const FrameRing&) = delete;

        /// How many frames have been submitted, which is the number the next one will carry.
        std::uint64_t getRecording() const { return mFrame; }

        /// The slot the frame being recorded uses, for whatever else keeps one of a thing per
        /// frame in flight.
        FrameSlot getRecordingSlot() const { return FrameSlot{ static_cast<std::uint32_t>(mFrame % sFrameSlots) }; }

        /// The frame being recorded, begun if it was not: the frame that last used its slot is
        /// waited for, its timer cleared, and its counts where they come back. A miss count is an
        /// atomic sum over the frame, so the block starts each one at nothing — and it is not
        /// started at all where nothing reads it back.
        FrameRecord& begin();

        /// A command buffer for one placement of `frame`, made on the frame that first needs it.
        VkCommandBuffer takePlaceCommands(FrameRecord& frame);

        /// Records a copy of `target`, as the frame's passes left it, into host memory of the
        /// ring's for the report that comes back with the frame — `FrameResult::mPixels`. After the
        /// last pass that writes `target`.
        void readPicture(FrameRecord& frame, VkCommandBuffer commands, const Image& target);

        /// Records the fold of `channels` and of what the denoisers wrote, null where a filter did not
        /// run, into the frame's digest, and notes `facts` for the report — `FrameResult::mDigest`,
        /// whose words are read once the frame is waited for.
        void readDigest(FrameRecord& frame, VkCommandBuffer commands,
            const std::array<const Image*, Shaders::DIGEST_IMAGES>& channels, std::uint32_t channelsInFields,
            const std::array<const Image*, sDenoiserImageCount>& denoiser, VkExtent2D extent, std::uint32_t frameNumber,
            const FrameDigest& facts, GpuTimer* timer);

        /// The recording of `frame`'s trace, begun on the frame's own buffer. An exception that
        /// leaves it unsubmitted ends the renderer: the passes recorded into it already took their
        /// turns (g-buffer, histories, ripples, waves, sprites, previous camera), so a later trace
        /// would bind held surfaces never written. Nothing catches one.
        Recording recordWorld(FrameRecord& frame);

        /// Submits what a frame recorded into `world`, `recordWorld`'s, and counts it as in flight,
        /// with its counts ordered for the host after every pass that could have written them.
        void submit(FrameRecord& frame, Recording&& world);

        /// Whether the frame being recorded was begun and not yet submitted.
        bool isOpen() const { return mSlots.at(getRecordingSlot()).mState.get() == FrameState::Begun; }

        /// Closes the open frame with an empty trace: submitted and counted, so its placements'
        /// buffers come back once it is waited for, and no report.
        void skip();

        /// The oldest report in hand, waiting a frame out for one where there is none —
        /// `Renderer::finishFrame`.
        std::optional<FrameResult> finishFrame();

        /// The oldest report in hand, waiting only where the ring has no room for the next frame
        /// — `Renderer::collectFrame`.
        std::optional<FrameResult> collectFrame();

        /// Waits for every frame in flight. What a rebuild, a resize and a wait on the pictures
        /// inside the interface do first. Not an arrival, which is written behind the frames in
        /// flight (`VulkanRenderer::extendScene`).
        void finishAll();

        /// Drops what nothing has collected, for a caller whose world has gone. Their stores that
        /// were not finite are not dropped with them: the next report carries them (`mDropped`).
        void dropReports();

        /// Waits every frame in flight out and hands over every store not finite the census counted
        /// for the ring's frames since the last call, whatever collected their reports, and drops
        /// every uncollected report: what a test asks as it ends, so one that never read a report's
        /// stores is not passed and the next reads nothing from before it.
        NotFinite takeCountedStores();

    private:
        /// The slot of the frame being recorded, with whatever last used it finished. It does not
        /// open the frame, which `begin` is for.
        FrameRecord& recording();

        /// The slot `frame` used.
        FrameRecord& slotOf(std::uint64_t frame)
        {
            return mSlots.at(FrameSlot{ static_cast<std::uint32_t>(frame % sFrameSlots) });
        }

        /// Where frame `frame`'s picture lands where `FrameOptions::mReadBack` asks, grown to the
        /// picture on the first frame that asks and kept.
        ///
        /// One more than the slots, and not the slot's own: a report is collected before the frame
        /// that reuses its slot is drawn and read after it (`RtxRenderer` collects, traces, then
        /// hands the report on), so a picture in the slot's memory was torn by that frame's copy.
        /// With one more picture than slots, `FrameResult::mPixels` stands until the `renderFrame`
        /// after the one it was collected before.
        GrowableBuffer& pictureOf(std::uint64_t frame) { return mPictures[frame % mPictures.size()]; }

        /// Waits the oldest frame in flight out and puts what it came to in `mReports`.
        void finishOldest();

        /// Waits until the ring has a slot for the next frame.
        void makeRoom();

        void close(FrameRecord& frame, bool traced, Recording&& world);

        std::optional<FrameResult> takeReport();

        const Device& mDevice;

        /// Whether a cut walk was logged: once a run, since a walk that never ends does not end on
        /// any frame after the first either.
        bool mWalkCutLogged = false;

        PerSlot<FrameRecord> mSlots;
        std::array<GrowableBuffer, sFrameSlots + 1> mPictures;

        /// What folds a frame's images into its digest, on the frames that ask; none in a ring that
        /// reads nothing back.
        std::optional<DigestPass> mDigest;

        /// The next frame to record and the next to finish. Everything from `mFinished` to `mFrame`
        /// is in flight, and there are never more of those than there are slots.
        std::uint64_t mFrame = 0;
        std::uint64_t mFinished = 0;

        /// What frames have come to and nothing has asked for yet, oldest first. Never longer than
        /// `sFrameSlots`: a caller that stopped asking is not a reason to grow, and the oldest is
        /// the one furthest from what it asks about next, so `finishOldest` drops it.
        std::vector<FrameResult> mReports;

        /// The stores not finite of the reports dropped since the ring last made one, which the
        /// next report it makes carries: each is a logic error, and a caller that stopped asking
        /// for a while has not stopped asking whether a frame stored one.
        NotFinite mDropped;

        /// The stores not finite of every frame since `takeCountedStores` last took them, each once,
        /// whatever became of its report.
        NotFinite mCounted;
    };
}
