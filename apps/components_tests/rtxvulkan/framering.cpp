#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/owned.hpp>
#include <components/rtxvulkan/framering.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxFrameRingTest : Testing::DeviceTest
        {
            /// Records nothing and submits it, which is a frame as far as the ring is concerned.
            ///
            /// **Empty on purpose.** What is under test is the ring's account of which slot belongs
            /// to whom, and a command buffer with work in it would only make the fences slower.
            FrameRecord& submitEmpty(FrameRing& ring)
            {
                FrameRecord& frame = ring.begin();
                getPool().begin(frame.mWorld.mCommands);
                ring.submit(frame);
                return frame;
            }
        };

        /// The slot the ring hands out for recording is never one a frame in flight still owns.
        ///
        /// **Two slots and two frames in flight makes them the same slot**, which is the whole of
        /// this: the slot of the frame being recorded and that of the oldest in flight agree once
        /// the ring is full, so the slot a caller is about to record into is the oldest frame's,
        /// and the next drain waits that frame alone while the newer one is still tracing.
        ///
        /// The ring is filled first, and `FrameState::Submitted` is what says a frame is still on
        /// the queue.
        TEST_F(RtxFrameRingTest, theSlotHandedOutForRecordingIsNotOneAFrameInFlightHolds)
        {
            const bool countHits = false;
            FrameRing ring(getDevice(), countHits);

            // Filled to the brim: nothing collects, so every frame stays in flight, exactly as
            // a watched window (`view`) leaves the ring.
            FrameRecord& first = submitEmpty(ring);
            for (std::uint32_t frame = 1; frame < sFrameSlots; ++frame)
                submitEmpty(ring);

            EXPECT_EQ(ring.getRecording(), sFrameSlots) << "the ring did not take the frames";
            EXPECT_EQ(first.mState.get(), FrameState::Submitted) << "the first frame was finished by something";

            // The first frame's slot, begun: from `Idle` alone, so the frame on it was waited for.
            FrameRecord& next = ring.begin();
            EXPECT_EQ(&next, &first) << "the slot handed out is not the oldest frame's";
            EXPECT_EQ(next.mState.get(), FrameState::Begun);

            // And the same answer to the same question, which is what a caller placing twice asks.
            EXPECT_EQ(&ring.begin(), &next) << "beginning the frame again moved to another slot";
            getPool().begin(next.mWorld.mCommands);
            ring.submit(next);

            // Before the ring goes, because its command buffers go with it and the last frame is
            // still on the queue.
            ring.finishAll();
        }

        /// What is buried while a frame is in flight is held until a submit made after the burial
        /// has run, whoever buried it and whatever the ring was doing, and a wait ends a prefix of
        /// the burials in the order they were made.
        ///
        /// **The property the two graveyards keyed by slot did not have.** A burial went into the
        /// recording frame's, and that frame's wait was what freed it — so a burial made into the
        /// wrong slot, or a table replaced by a caller that never saw a graveyard, was an object
        /// gone from under a trace. One graveyard stamps every burial with the next submit's value,
        /// so nothing is freed before every submit that could name it has finished.
        ///
        /// **In burial order**, because an owner's destructor buries what must end first first: a
        /// view before its image, a structure before the storage it stands in. Handles 1 and 2 are
        /// buried under the frame that carries them and 3 under the frame after, and an end that
        /// notes each handle says which went when.
        TEST_F(RtxFrameRingTest, aBurialOutlivesEverySubmitMadeBeforeItAndEndsInOrder)
        {
            // The device's graveyard is shared with every test before this one and with what the
            // ring made, so it is emptied after both: the counts below are of this test's burials
            // alone.
            FrameRing ring(getDevice(), false);
            Graveyard& graveyard = getDevice().getGraveyard();
            getDevice().waitIdle();
            getDevice().collectIdle();

            static std::vector<std::uint64_t> ended;
            ended.clear();
            const EndHandle note = [](const Device&, const std::uint64_t handle) { ended.push_back(handle); };

            // One frame on the queue, and a buffer let go of while it is: its handle and its memory
            // are one burial.
            submitEmpty(ring);
            {
                const Buffer dropped = Buffer::hostWritten(getDevice(), 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "test");
            }
            EXPECT_EQ(graveyard.getHeldCount(), 1u) << "a buffer is its handle and its memory, buried together";

            // That frame done is not enough: the burial is stamped with the value of the submit
            // after it, which nothing has made.
            ring.finishAll();
            EXPECT_EQ(graveyard.getHeldCount(), 1u) << "freed before a submit made after the burial had run";

            graveyard.bury(note, 1, DeviceMemory());
            graveyard.bury(note, 2, DeviceMemory());
            submitEmpty(ring);
            graveyard.bury(note, 3, DeviceMemory());
            EXPECT_EQ(graveyard.getHeldCount(), 4u);

            // The submit that carries 1 and 2 run, and the buffer with them; 3 waits for the next.
            ring.finishAll();
            EXPECT_EQ(ended, (std::vector<std::uint64_t>{ 1, 2 }));
            EXPECT_EQ(graveyard.getHeldCount(), 1u) << "held past the submit that retired it";

            submitEmpty(ring);
            ring.finishAll();
            EXPECT_EQ(ended, (std::vector<std::uint64_t>{ 1, 2, 3 }));
            EXPECT_EQ(graveyard.getHeldCount(), 0u);

            // An image is its view, and then its handle with its memory.
            {
                const Image dropped(getDevice(), 4, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, "test");
            }
            EXPECT_EQ(graveyard.getHeldCount(), 2u);
            submitEmpty(ring);
            ring.finishAll();
            EXPECT_EQ(graveyard.getHeldCount(), 0u);
        }

        /// A frame that left its picture comes back with it, and one that did not comes back with
        /// none. The picture stands past the frame that takes the slot: a report is collected
        /// before that frame is drawn and read after it, and the memory it reads is the frame
        /// after's to write over.
        TEST_F(RtxFrameRingTest, aPictureStandsPastTheFrameThatTakesItsSlot)
        {
            const Device& device = getDevice();
            FrameRing ring(device, false);

            constexpr std::array<std::uint8_t, 4> picture{ 1, 2, 3, 4 };
            constexpr std::array<std::uint8_t, 4> other{ 5, 6, 7, 8 };

            // A frame whose target is one texel of `bytes`, read back as the renderer reads its
            // picture. Each channel is cleared to `byte / 255`, which the format rounds back to
            // the byte exactly.
            const auto leave = [&](FrameRecord& frame, const std::span<const std::uint8_t> bytes) {
                const Image target(device, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    "test target");
                const VkClearColorValue colour{ .float32
                    = { bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f, bytes[3] / 255.0f } };

                const VkCommandBuffer commands = frame.mWorld.mCommands;
                getPool().begin(commands);
                target.clear(commands, Use::sUndefined, colour, Use::sComputeWrite);
                ring.readPicture(frame, commands, target);
                ring.submit(frame);
            };

            leave(ring.begin(), picture);
            submitEmpty(ring);

            // Collected before the frame that takes its slot, as the renderer does.
            const std::optional<FrameResult> came = ring.collectFinished();
            ASSERT_TRUE(came.has_value());
            EXPECT_EQ(came->mFrame, 0u);
            ASSERT_EQ(came->mPixels.size(), picture.size());
            EXPECT_TRUE(std::equal(came->mPixels.begin(), came->mPixels.end(), picture.begin()));

            // The slot begun again forgets what its last frame left, so a stale picture cannot be
            // read off a frame that did not ask — and the picture it leaves lands elsewhere.
            FrameRecord& third = ring.begin();
            EXPECT_EQ(third.mReadBackBytes, 0u);
            leave(third, other);
            EXPECT_TRUE(std::equal(came->mPixels.begin(), came->mPixels.end(), picture.begin()))
                << "the frame that took the slot wrote over the picture";

            const std::optional<FrameResult> next = ring.collect();
            ASSERT_TRUE(next.has_value());
            EXPECT_EQ(next->mFrame, 1u);
            EXPECT_TRUE(next->mPixels.empty()) << "a frame that asked for no picture came back with one";

            const std::optional<FrameResult> after = ring.collect();
            ASSERT_TRUE(after.has_value());
            EXPECT_EQ(after->mFrame, 2u);
            ASSERT_EQ(after->mPixels.size(), other.size());
            EXPECT_TRUE(std::equal(after->mPixels.begin(), after->mPixels.end(), other.begin()));
        }

        /// **A frame the host refused to trace closes, and the next placement takes the next
        /// slot.** Placed and skipped frame after frame, each slot holds the one placement buffer
        /// its own frame took, where a frame left open took one more at every placement. A skipped
        /// frame is numbered and waited for and comes back with no report, so the traced frame
        /// after three of them is the one report, under the number it was submitted with.
        TEST_F(RtxFrameRingTest, aSkippedFrameClosesAndComesBackWithNoReport)
        {
            FrameRing ring(getDevice(), false);

            constexpr std::uint64_t skipped = 3;
            for (std::uint64_t at = 0; at < skipped; ++at)
            {
                FrameRecord& frame = ring.begin();
                const VkCommandBuffer placement = ring.takePlaceCommands(frame);
                getPool().begin(placement);
                getPool().submit(placement);

                EXPECT_TRUE(ring.isOpen());
                ring.skip();
                EXPECT_FALSE(ring.isOpen());
                EXPECT_EQ(frame.mPlaceCommands.size(), 1u) << "frame " << at;
            }

            EXPECT_EQ(ring.getRecording(), skipped) << "a skipped frame went unnumbered";

            submitEmpty(ring);
            const std::optional<FrameResult> traced = ring.collect();
            ASSERT_TRUE(traced.has_value()) << "the skipped frames stood in front of the traced one";
            EXPECT_EQ(traced->mFrame, skipped);
            EXPECT_FALSE(ring.collect().has_value()) << "a skipped frame came back with a report";
        }
    }
}
