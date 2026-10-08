#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/framezone.hpp>

#include "device.hpp"
#include "handles.hpp"

namespace Rtx
{
    class GpuZone;
    class GpuZones;

    /// Timestamps written into the command stream, so a frame can say where its device time went,
    /// where a wall clock around a submit measures one number for eight pieces of work. Both ends
    /// wait for every stage, so a zone cannot overlap its neighbours — which would distort a
    /// renderer whose passes overlap, and this one puts a full barrier between every pass already.
    /// Zones may span several command buffers, because each reserves and resets only the pair of
    /// queries it writes.
    class GpuTimer
    {
    public:
        explicit GpuTimer(const Device& device);

        /// Forgets the last frame's zones. Whatever is opened after this is one report, and
        /// `frame` is what a checkpoint the zones set names it as.
        void beginFrame(std::uint64_t frame = 0);

        /// What the zones measured, in the order they were opened, into `into`. The caller waited
        /// for every submit the zones were recorded into.
        void resolve(GpuZones& into);

        // Read by the tests and by nothing else.
        /// The queries the zones write, two a zone in the order they were opened.
        VkQueryPool getQueryPool() const { return mHandle.get(); }

    private:
        /// A zone is opened and closed by a `GpuZone` alone, which cannot leave one open.
        friend class GpuZone;

        /// Opens a zone. Also names the region for a capture, where the build and the instance
        /// carry the labels.
        void open(VkCommandBuffer commands, FrameZone zone);

        /// Closes the zone `open` started. Every open is closed before the next is opened.
        void close(VkCommandBuffer commands);

        const Device& mDevice;
        QueryPool mHandle;

        /// Nanoseconds a tick of the device's clock is worth, and how many of its bits count.
        double mPeriod = 1.0;
        std::uint64_t mMask = ~std::uint64_t{ 0 };
        bool mSupported = false;

        /// The first of the two queries bracketing a zone, and the checkpoint the queue was set
        /// at its open. The checkpoint is in here rather than beside it because the queue keeps a
        /// pointer to it: `mZones` is reserved to `sMaxGpuZones` and never grows past that, so an
        /// element stays where it is until the slot's next `beginFrame`.
        struct Zone
        {
            Checkpoint mCheckpoint;
            FrameZone mZone = FrameZone::Count;
            std::uint32_t mFirstQuery = 0;
        };

        std::vector<Zone> mZones;
        std::uint64_t mFrame = 0;

        /// Which of `mZones` is open, or `mZones.size()` for none.
        std::size_t mOpen = 0;
    };

    /// Brackets a piece of work where there is a timer to bracket it with: opened where it is made
    /// and closed where it goes, so no path out of the work, a return or a throw, leaves the zone
    /// open. Nothing for a null timer: a scene arriving and a picture inside the interface record
    /// the same commands and are not frames, so zones opened there would land in whichever frame
    /// report came next. A zone that must end before the work after it is made in a block of its
    /// own.
    class [[nodiscard]] GpuZone
    {
    public:
        GpuZone(GpuTimer* timer, VkCommandBuffer commands, FrameZone zone)
            : mTimer(timer)
            , mCommands(commands)
        {
            if (mTimer != nullptr)
                mTimer->open(commands, zone);
        }

        ~GpuZone()
        {
            if (mTimer != nullptr)
                mTimer->close(mCommands);
        }

        GpuZone(const GpuZone&) = delete;
        GpuZone(GpuZone&&) = delete;
        GpuZone& operator=(const GpuZone&) = delete;
        GpuZone& operator=(GpuZone&&) = delete;

    private:
        GpuTimer* const mTimer;
        const VkCommandBuffer mCommands;
    };
}
