#include "gputimer.hpp"

#include <array>
#include <cassert>
#include <format>
#include <string_view>

#include <volk.h>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "result.hpp"

namespace Rtx
{
    GpuTimer::GpuTimer(const Device& device, const bool timing)
        : mDevice(device)
    {
        const VkPhysicalDeviceLimits& limits
            = device.getPhysicalDevice().getProperties().mProperties2.properties.limits;

        // How many of the queue's timestamp bits are meaningful, which is zero where the queue
        // cannot timestamp at all — asked of the device once, when it was chosen.
        const std::uint32_t bits = device.getPhysicalDevice().getTimestampBits();

        // Before anything can return: `open` files a zone whether or not the queue times it, and
        // hands its checkpoint's address to the queue, which a vector that grew would move.
        mZones.reserve(sMaxGpuZones);

        // A period of zero is the driver saying its clock does not advance, which no amount of
        // arithmetic recovers from.
        mTimes = timing && bits > 0 && limits.timestampPeriod > 0.0f;
        if (!mTimes)
            return;

        mPeriod = limits.timestampPeriod;
        mMask = bits >= 64 ? ~std::uint64_t{ 0 } : (std::uint64_t{ 1 } << bits) - 1;

        const VkQueryPoolCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = sMaxGpuZones * 2,
            .pipelineStatistics = 0,
        };

        mHandle = QueryPool::make(device, vkCreateQueryPool, info, "vkCreateQueryPool");
        device.setName(mHandle.get(), "frame timestamps");
    }

    void GpuTimer::beginFrame(const std::uint64_t frame)
    {
        mZones.clear();
        mOpen = 0;
        mFrame = frame;
    }

    void GpuTimer::open(VkCommandBuffer commands, const FrameZone zone)
    {
        const std::string_view name = sFrameZoneNames.name(zone);
        mDevice.beginLabel(commands, name.data());

        assert(mOpen == mZones.size() && "a zone was opened while another was still open");

        // **A frame that wants more zones than the pool holds broke `sMaxGpuZones`**, which counts
        // what one frame opens, and a report short of its last zones would say nothing of it. Left
        // out past the assert rather than written past the pool; the label above still names it for
        // a capture.
        assert(mZones.size() < sMaxGpuZones && "a frame opened more zones than `sMaxGpuZones` counts");
        if (mZones.size() >= sMaxGpuZones)
            return;

        const auto first = static_cast<std::uint32_t>(mZones.size()) * 2;
        const Zone& opened = mZones.emplace_back(
            Zone{ .mCheckpoint = Checkpoint{ .mName = name, .mFrame = mFrame }, .mZone = zone, .mFirstQuery = first });
        mDevice.checkpoint(commands, &opened.mCheckpoint);

        if (!mTimes)
            return;

        // Reset here rather than once per command buffer. The zones of one frame are spread over
        // three submits and this class is not told where the boundaries are; resetting the pair
        // about to be written, in the buffer about to write it, is correct wherever it lands.
        vkCmdResetQueryPool(commands, mHandle.get(), first, 2);
        vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, mHandle.get(), first);
    }

    void GpuTimer::close(VkCommandBuffer commands)
    {
        mDevice.endLabel(commands);

        if (mOpen == mZones.size())
            return;

        if (mTimes)
            vkCmdWriteTimestamp2(
                commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, mHandle.get(), mZones[mOpen].mFirstQuery + 1);
        ++mOpen;
    }

    void GpuTimer::resolve(GpuZones& into)
    {
        assert(mOpen == mZones.size() && "a zone was left open when the frame was resolved");

        into.clear();
        if (mZones.empty() || !mTimes)
            return;

        // A tick and its availability for each query.
        std::array<std::uint64_t, sMaxGpuZones * 2 * 2> results{};
        const auto count = static_cast<std::uint32_t>(mZones.size()) * 2;

        // **No wait for availability.** Every submit these were written into has already been
        // waited for, so every query a zone wrote is there; a wait could only be for a query no
        // submit will ever write — a zone in a batch that was discarded — and would last for ever,
        // outside every patience the device keeps. So an unwritten query is read as one, and it is
        // this code's broken contract and not a fault of the device's.
        const VkResult read = vkGetQueryPoolResults(mDevice.getHandle(), mHandle.get(), 0, count,
            count * 2 * sizeof(std::uint64_t), results.data(), 2 * sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (read != VK_NOT_READY)
            checkVk(mDevice, read, "vkGetQueryPoolResults");

        for (const Zone& zone : mZones)
        {
            const std::uint32_t opened = zone.mFirstQuery * 2;
            const std::uint32_t closed = opened + 2;
            if (results[opened + 1] == 0 || results[closed + 1] == 0)
                Crash::fatal(std::format("the GPU timer's {} zone was resolved before a submit wrote its timestamps",
                    sFrameZoneNames.name(zone.mZone)));

            const std::uint64_t began = results[opened] & mMask;
            const std::uint64_t ended = results[closed] & mMask;

            // The masked counter wraps, and a frame is nanoseconds against a counter that is at
            // least thirty-six bits: the difference is the elapsed time whichever side of a wrap the
            // two landed.
            const std::uint64_t elapsed = (ended - began) & mMask;

            into.add(GpuSpan{ .mZone = zone.mZone, .mMs = static_cast<double>(elapsed) * mPeriod / 1.0e6 });
        }
    }
}
