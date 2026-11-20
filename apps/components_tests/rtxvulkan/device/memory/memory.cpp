#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/memorylimits.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/blockedbuffer.hpp>
#include <components/rtxvulkan/device/memory/contentarena.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/owned.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/result.hpp>

namespace Rtx
{
    namespace
    {
        /// **Each test starts with the graveyard empty and the driver's figures fresh**: a range an earlier test let
        /// go stays counted until a collection gives it back, and the library's usage is its own tally since the
        /// driver last said, which the driver's next word corrects by what an earlier test left it. A test that reads
        /// the heap's held memory before and after a take counted that correction when the take fetched the
        /// driver's figures (the GPU suite in seed 44490's order).
        struct RtxMemoryTest : Testing::DeviceTest
        {
            void SetUp() override
            {
                DeviceTest::SetUp();
                getDevice().waitIdle();
                getDevice().collectIdle();
                getDevice().getMemory().refreshBudget(getDevice().getTimeline().getNext());
            }
        };

        /// A buffer of `size` bytes and the room the allocator gave it, held together so the room
        /// outlives what is bound to it. The allocator is asked for a real buffer's room, because
        /// that is the one way it is asked and what it is told about the buffer — a linear resource
        /// of this size — is what decides where the range lands.
        struct Bound
        {
            Owned<VkBuffer, vkDestroyBuffer> mBuffer;
            DeviceMemory mMemory;
        };

        /// A buffer's room as `use`, in memory that is `properties`, at `alignment` at least, bound.
        Bound bindAs(const Device& device, const VkDeviceSize size, const VkMemoryPropertyFlags properties,
            const MemoryUse use, const VkDeviceSize alignment = 1)
        {
            const VkBufferCreateInfo create{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = size,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            Bound bound;
            bound.mBuffer = Owned<VkBuffer, vkDestroyBuffer>::make(device, vkCreateBuffer, create, "vkCreateBuffer");
            bound.mMemory = device.getMemory().take(bound.mBuffer.get(), properties, alignment, use, "a test buffer");
            checkVk(device,
                vkBindBufferMemory(
                    device.getHandle(), bound.mBuffer.get(), bound.mMemory.getHandle(), bound.mMemory.getOffset()),
                "vkBindBufferMemory");

            return bound;
        }

        /// The same, as essential memory.
        Bound bind(const Device& device, const VkDeviceSize size, const VkMemoryPropertyFlags properties)
        {
            return bindAs(device, size, properties, MemoryUse::Essential);
        }

        /// **A device with no video memory is refused before anything is made on it**, so the
        /// refusal leaves nothing of the library behind: the properties of this device with every
        /// type's video-memory flag taken off.
        TEST_F(RtxMemoryTest, aDeviceWithNoVideoMemoryIsRefusedBeforeTheLibraryIsMade)
        {
            const Device& device = getDevice();
            VkPhysicalDeviceMemoryProperties none = device.getPhysicalDevice().getProperties().mMemory;
            for (std::uint32_t type = 0; type < none.memoryTypeCount; ++type)
                none.memoryTypes[type].propertyFlags
                    &= static_cast<VkMemoryPropertyFlags>(~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            EXPECT_THROW(MemoryAllocator(device, mHarness.mInstance->getHandle(),
                             device.getPhysicalDevice().getHandle(), none, MemoryExtensions{}),
                Unsupported);
        }

        /// A thousand small resources come out of one allocation rather than a thousand.
        ///
        /// **What the allocator exists for.** A cell of Morrowind brings a few hundred textures each with a
        /// two-kilobyte shading map, so an allocation apiece is four figures of kernel-visible calls, each rounded up
        /// to the driver's granularity. Two megabytes fit inside one block, so the honest claim is one.
        TEST_F(RtxMemoryTest, aThousandSmallResourcesComeOutOfOneAllocation)
        {
            MemoryAllocator& memory = getDevice().getMemory();
            const std::size_t before = memory.getBlockCount();

            std::vector<Bound> held;
            held.reserve(1000);
            for (std::int32_t at = 0; at < 1000; ++at)
                held.push_back(bind(getDevice(), 2048, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

            // At most one, and not exactly one: a test that ran before this may have left a block
            // with room in it, and taking that room is the allocator working.
            EXPECT_LE(memory.getBlockCount() - before, 1u)
                << "a thousand small resources made more than one allocation";

            // And they are a thousand distinct places in it: no two ranges of one block overlap.
            // Ordered by place first, because a block a test before this one left holes in is
            // filled from its holes, wherever they are, and not in the order the ranges were asked.
            std::vector<std::pair<VkDeviceMemory, VkDeviceSize>> places;
            places.reserve(held.size());
            for (const Bound& bound : held)
                places.emplace_back(bound.mMemory.getHandle(), bound.mMemory.getOffset());
            std::sort(places.begin(), places.end());

            for (std::size_t at = 1; at < places.size(); ++at)
            {
                if (places[at].first != places[at - 1].first)
                    continue;

                EXPECT_GE(places[at].second, places[at - 1].second + 2048)
                    << "two ranges overlap at " << places[at].second;
            }
        }

        /// A range given back is what the next resource of that size takes.
        ///
        /// **What makes a cell leaving pay for the cell arriving.** The hole a departing texture leaves is the hole
        /// the next one lands in: no driver call, and no block that grows for ever.
        TEST_F(RtxMemoryTest, aRangeGivenBackIsTakenOverByTheNextResource)
        {
            MemoryAllocator& memory = getDevice().getMemory();

            // What tests before this one buried, given back first: the hole it leaves is only the
            // next resource's where no other hole opens between the two.
            getDevice().waitIdle();
            getDevice().collectIdle();

            VkDeviceMemory handle = VK_NULL_HANDLE;
            VkDeviceSize offset = 0;
            {
                const Bound first = bind(getDevice(), 2048, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                handle = first.mMemory.getHandle();
                offset = first.mMemory.getOffset();
            }
            getDevice().waitIdle();
            getDevice().collectIdle();

            const std::size_t after = memory.getBlockCount();
            const Bound second = bind(getDevice(), 2048, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            EXPECT_EQ(second.mMemory.getHandle(), handle) << "the range came from a different allocation";
            EXPECT_EQ(second.mMemory.getOffset(), offset) << "the range that was given back was not taken over";
            EXPECT_EQ(memory.getBlockCount(), after) << "taking over a hole made a new allocation";
        }

        /// A buffer starts where its use owes, past the driver's alignment, out of either path; and buffers alone sit
        /// closer together than the device's buffer-image granularity.
        ///
        /// **The library is told a buffer is a buffer** (`vmaAllocateMemoryForBuffer`), so it keeps the granularity
        /// only where an image is a neighbour. Asked as a resource of no known kind, it kept a kilobyte between every
        /// two buffers on one card.
        TEST_F(RtxMemoryTest, aBufferStartsWhereItsUseOwesAndBuffersPackPastTheGranularity)
        {
            const Device& device = getDevice();
            constexpr VkDeviceSize sOwed = 4096;
            constexpr VkDeviceSize sSmall = 64;

            for (const MemoryUse use : { MemoryUse::Frame, MemoryUse::Essential })
            {
                std::vector<Bound> held;
                for (std::int32_t at = 0; at < 16; ++at)
                {
                    Bound bound = bindAs(device, sSmall, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, use, sOwed);
                    EXPECT_EQ(bound.mMemory.getOffset() % sOwed, 0u)
                        << "a buffer started off the alignment its use owes, as use " << static_cast<std::int32_t>(use);
                    held.push_back(std::move(bound));
                }
            }

            // The other side: at the driver's own alignment, which is finer than a page, sixty-four
            // small buffers pack. At least two consecutive ones in one block stand closer than the
            // granularity, where it is coarser than a buffer is long; where it is not, nothing
            // tells a buffer from a resource of no known kind.
            const VkDeviceSize granularity
                = device.getPhysicalDevice().getProperties().mProperties2.properties.limits.bufferImageGranularity;
            std::vector<Bound> packed;
            packed.reserve(64);
            for (std::int32_t at = 0; at < 64; ++at)
                packed.push_back(bind(device, sSmall, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

            std::vector<std::pair<VkDeviceMemory, VkDeviceSize>> places;
            places.reserve(packed.size());
            for (const Bound& bound : packed)
                places.emplace_back(bound.mMemory.getHandle(), bound.mMemory.getOffset());
            std::sort(places.begin(), places.end());

            VkDeviceSize closest = ~VkDeviceSize{ 0 };
            for (std::size_t at = 1; at < places.size(); ++at)
                if (places[at].first == places[at - 1].first)
                    closest = std::min(closest, places[at].second - places[at - 1].second);

            if (granularity > sSmall)
            {
                EXPECT_LT(closest, granularity) << "buffers alone were kept a granularity apart";
            }
            EXPECT_GE(closest, sSmall) << "two buffers overlap";
        }

        /// Two host-visible ranges are two windows on one mapping, and neither reaches the other.
        ///
        /// **A range is mapped as it is made and the pointer is its own**, which is how a caller
        /// reaches its memory — and the way to get it wrong is to hand every range the block's base.
        TEST_F(RtxMemoryTest, twoHostVisibleRangesAreSeparateWindowsOnOneMapping)
        {
            constexpr VkMemoryPropertyFlags staging
                = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            const Bound first = bind(getDevice(), 1024, staging);
            const Bound second = bind(getDevice(), 1024, staging);

            ASSERT_NE(first.mMemory.map(), nullptr);
            ASSERT_NE(second.mMemory.map(), nullptr);
            ASSERT_NE(first.mMemory.map(), second.mMemory.map()) << "two ranges were handed the same address";

            auto* const one = static_cast<std::uint8_t*>(first.mMemory.map());
            auto* const other = static_cast<std::uint8_t*>(second.mMemory.map());

            for (std::int32_t at = 0; at < 1024; ++at)
            {
                one[at] = 0x11;
                other[at] = 0x22;
            }

            EXPECT_EQ(one[0], 0x11) << "the second range wrote over the first";
            EXPECT_EQ(one[1023], 0x11) << "the second range wrote over the end of the first";
            EXPECT_EQ(other[0], 0x22);

            // And the address is the block's own plus the range's offset, which is what the caller
            // is promised: the two differ by exactly the difference of their offsets.
            if (first.mMemory.getHandle() == second.mMemory.getHandle())
            {
                EXPECT_EQ(
                    other - one, static_cast<std::ptrdiff_t>(second.mMemory.getOffset() - first.mMemory.getOffset()))
                    << "a range's address is not its block's plus its offset";
            }
        }

        /// The report accounts for every block, and a range taken moves the live figure and not the reserved one.
        ///
        /// **The figure has to be real before anything can be decided on it.** Reserved is what `vkAllocateMemory`
        /// asked for, live is what is inside it, and one never exceeds the other.
        TEST_F(RtxMemoryTest, theReportCountsWhatWasReservedAndWhatIsLive)
        {
            MemoryAllocator& memory = getDevice().getMemory();

            const MemoryReport before = memory.report();
            ASSERT_GT(before.mHeapCount, 0u) << "a device with no memory heaps";

            const auto live = [](const MemoryReport& report) {
                std::uint64_t total = 0;
                for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
                    total += report.mHeaps[heap].mLive;

                return total;
            };

            // Half a block, so the range cannot come out of a hole an earlier test left and cannot
            // help but be visible in the live figure.
            constexpr VkDeviceSize wanted = 4 * 1024 * 1024;
            const Bound held = bind(getDevice(), wanted, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            const MemoryReport during = memory.report();
            EXPECT_EQ(during.mHeapCount, before.mHeapCount);
            EXPECT_GE(live(during), live(before) + wanted)
                << "a range of " << wanted << " bytes did not show in the live figure";

            for (std::uint32_t heap = 0; heap < during.mHeapCount; ++heap)
            {
                const HeapUse& use = during.mHeaps[heap];
                EXPECT_LE(use.mLive, use.mReserved) << "heap " << heap << " holds more than it reserved";
                EXPECT_LE(use.mReserved, use.mSize) << "heap " << heap << " reserved more than it has";
                EXPECT_EQ(use.mBlocks == 0, use.mReserved == 0) << "heap " << heap << " counted blocks and no bytes";
            }

            std::uint32_t blocks = 0;
            for (std::uint32_t heap = 0; heap < during.mHeapCount; ++heap)
                blocks += during.mHeaps[heap].mBlocks;
            EXPECT_EQ(blocks, memory.getBlockCount()) << "the report left a block out";
        }

        /// **Every heap the device states is in the report, as the device states it**: its size, and
        /// whether it is video memory, which on a card with video memory of its own is what tells it
        /// from the system's.
        TEST_F(RtxMemoryTest, theReportStatesEveryHeapAsTheDeviceDoes)
        {
            const VkPhysicalDeviceMemoryProperties& properties
                = getDevice().getPhysicalDevice().getProperties().mMemory;

            const MemoryReport report = getDevice().getMemory().report();
            ASSERT_EQ(report.mHeapCount, properties.memoryHeapCount);
            for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
            {
                EXPECT_EQ(report.mHeaps[heap].mSize, properties.memoryHeaps[heap].size) << "heap " << heap;
                EXPECT_EQ(report.mHeaps[heap].mDeviceLocal,
                    (properties.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
                    << "heap " << heap;
            }
        }

        /// Blocks that empty go back to the device.
        ///
        /// **What a load's staging costs after the load.** A world's geometry goes to the device through 184 MiB of
        /// staging blocks, measured at Seyda Neen, and nothing reads them again, so an emptied block is returned
        /// rather than kept until the renderer closes.
        TEST_F(RtxMemoryTest, blocksThatEmptyGoBackToTheDevice)
        {
            MemoryAllocator& memory = getDevice().getMemory();
            constexpr VkMemoryPropertyFlags staging
                = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            const std::size_t before = memory.getBlockCount();

            // Larger than a block, so each range takes an allocation of its own whatever the tests
            // before this left standing in the shared device.
            constexpr VkDeviceSize alone = 96 * 1024 * 1024;

            std::vector<Bound> held;
            held.reserve(3);
            for (std::int32_t at = 0; at < 3; ++at)
                held.push_back(bind(getDevice(), alone, staging));

            const std::size_t grown = memory.getBlockCount();
            ASSERT_EQ(grown, before + 3) << "three ranges too large to share did not make three blocks";

            held.clear();
            getDevice().waitIdle();
            getDevice().collectIdle();

            EXPECT_LT(memory.getBlockCount(), grown) << "emptied blocks were kept";
        }

        /// **The arena's cap keeps room for the largest growth one essential owner owes, and the frame's reserve beyond
        /// its targets** (`MemoryAllocator::getOwedOutside`, `ContentArena::capUnder`): a table made at a size owes
        /// twice that, its next growth, and made again at twice it owes four times; let go of, it owes nothing. A
        /// reserve exactly the targets owes nothing past the largest growth, and one two blocks past them lowers the
        /// cap by those two blocks, nothing else moving.
        TEST_F(RtxMemoryTest, theArenasCapKeepsRoomForTheLargestEssentialGrowthAndTheFramesReserve)
        {
            Device& device = getDevice();
            MemoryAllocator& memory = device.getMemory();
            memory.refreshBudget(device.getTimeline().getNext());
            const Testing::BudgetLimit roomy(memory, Testing::budgetForCap(device, VkDeviceSize{ 4 } << 30));
            const VkDeviceSize essential = memory.getLargestOwed();
            EXPECT_EQ(memory.report().mUses.mEssentialGrowth, essential);

            // Larger than any growth the device owes already, so its own is the largest.
            const VkDeviceSize sized = std::max(VkDeviceSize{ 1 } << 20, essential);
            {
                GrowableBuffer table(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "owed table");
                table.growTo(sized);
                const VkDeviceSize made = table.get().getSize();
                ASSERT_GT(2 * made, essential);
                EXPECT_EQ(memory.getLargestOwed(), 2 * made) << "its next growth, the largest";
                table.outgrow(made + 1);
                EXPECT_EQ(table.get().getSize(), 2 * made) << "a growth doubles";
                EXPECT_EQ(memory.getLargestOwed(), 4 * made);
            }
            EXPECT_EQ(memory.getLargestOwed(), essential) << "a table let go of still owed";

            const Image target(MemoryUse::Frame, device,
                ImageDescription{ .mWidth = 1024,
                    .mHeight = 1024,
                    .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                    .mUsage = VK_IMAGE_USAGE_STORAGE_BIT },
                "frame target");
            const VkDeviceSize frame = memory.getHeld(memory.getVideoHeap(), MemoryUse::Frame);
            const VkDeviceSize reserve = memory.getFrameReserve();
            memory.reserveFrame(frame);
            EXPECT_EQ(memory.getOwedOutside(), essential) << "a reserve the targets fill owes nothing more";
            const VkDeviceSize targeted = device.getContent().capUnder(memory);
            memory.reserveFrame(frame + 2 * sContentBlockBytes);
            EXPECT_EQ(memory.getOwedOutside(), essential + 2 * sContentBlockBytes);
            EXPECT_EQ(targeted - device.getContent().capUnder(memory), 2 * sContentBlockBytes)
                << "the reserve beyond the targets";
            memory.reserveFrame(reserve);
            device.waitIdle();
            device.collectIdle();
        }

        /// **The device says whether its memory is the system's by its type**, which holds it to its share of the
        /// system's memory (`SystemShare`).
        TEST_F(RtxMemoryTest, aUnifiedDeviceIsToldByItsType)
        {
            EXPECT_EQ(getDevice().getMemory().isUnified(),
                getDevice().getPhysicalDevice().getProperties().mProperties2.properties.deviceType
                    == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        }

        /// **A block of the arena is content's, counted as it is made and off as it goes, and essential memory is made
        /// under a budget of nought**: a block of a megabyte in content's first type holds a megabyte as content and
        /// moves no other use; given back at once, it holds nothing. The frame's tables are never refused.
        TEST_F(RtxMemoryTest, anArenasBlockIsContentsAndEssentialMemoryIsMadeUnderABudgetOfNought)
        {
            MemoryAllocator& memory = getDevice().getMemory();
            const std::uint32_t heap = memory.getVideoHeap();
            const auto held = [&] {
                return std::array{ memory.getHeld(heap, MemoryUse::Frame), memory.getHeld(heap, MemoryUse::Essential),
                    memory.getHeld(heap, MemoryUse::Content) };
            };
            const std::array before = held();

            const auto type = static_cast<std::uint32_t>(std::countr_zero(memory.getContentTypes()));
            std::optional<DeviceMemory> block = memory.takeBlock(type, VkDeviceSize{ 1 } << 20);
            ASSERT_TRUE(block.has_value());
            EXPECT_EQ(block->getSize(), VkDeviceSize{ 1 } << 20);
            const std::array during = held();
            EXPECT_EQ(during[2] - before[2], VkDeviceSize{ 1 } << 20);
            EXPECT_EQ(during[1], before[1]) << "a block was counted as essential memory";
            EXPECT_EQ(during[0], before[0]) << "a block was counted as the frame's";
            memory.giveBlock(std::move(*block));
            EXPECT_EQ(held()[2], before[2]) << "a block given back was still counted";

            const Testing::BudgetLimit none(memory, 0);
            const Bound essential = bind(getDevice(), VkDeviceSize{ 40 } << 20, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            EXPECT_NE(essential.mMemory.getHandle(), VK_NULL_HANDLE);
        }

        /// What each use holds is the size of each range it was given, counted as it is made and
        /// taken off as it goes, and no other use's figure moves.
        TEST_F(RtxMemoryTest, whatEachUseHoldsIsItsRangesExactly)
        {
            MemoryAllocator& memory = getDevice().getMemory();
            const std::uint32_t heap = memory.getVideoHeap();

            const auto held = [&] {
                return std::array{ memory.getHeld(heap, MemoryUse::Frame), memory.getHeld(heap, MemoryUse::Essential) };
            };
            const std::array before = held();

            // What the driver asks for a megabyte, which is the size of the range the allocator hands
            // back for it: the allocator places at the requirement and never rounds the size.
            std::optional<Bound> table
                = bindAs(getDevice(), 1 << 20, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryUse::Essential);
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(getDevice().getHandle(), table->mBuffer.get(), &requirements);

            const std::array during = held();
            EXPECT_EQ(during[1] - before[1], requirements.size);
            EXPECT_EQ(during[0], before[0]) << "a table's range was counted as the frame's";

            const Bound target = bindAs(getDevice(), 1 << 20, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryUse::Frame);
            EXPECT_EQ(held()[0] - before[0], requirements.size);

            // Let go, the range is buried and stays counted until the graveyard gives it back. **The heap holds it no
            // more**, buried or given back: it goes back once its frames are done (`MemoryAllocator::getVideoBudget`).
            const VkDeviceSize heldWith = memory.getVideoBudget().mHeld;
            table.reset();
            EXPECT_EQ(held()[1] - before[1], requirements.size) << "a range given back before any submit ran";
            EXPECT_EQ(heldWith - memory.getVideoBudget().mHeld, requirements.size) << "a buried range was held";
            getDevice().waitIdle();
            getDevice().collectIdle();
            EXPECT_EQ(held()[1], before[1]) << "a range given back was still counted";
        }
    }
}
