#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <volk.h>

namespace Rtx
{
    class BufferMarkers;
    class Device;

    /// What a checkpoint on the queue points at: the zone the timer opened and the frame it was
    /// opened for, so a device loss can say "frame 83, `tlas`". Owned by the timer that set it
    /// and stable while the timer's slot lives, which is longer than a fault takes to be reported.
    struct Checkpoint
    {
        std::string_view mName;
        std::uint64_t mFrame = 0;
    };

    /// The bit a checkpoint's `mFrame` carries where it counts pictures and not frames: a map tile or
    /// the doll traced inside the interface (`PictureTracer`).
    inline constexpr std::uint64_t sPictureCheckpoint = std::uint64_t{ 1 } << 63;

    /// **What `VK_EXT_device_fault` says, as lines of a lost device's report**: the driver's
    /// description, each address with what it was used for and how precisely it is known, and each
    /// of the vendor's codes.
    std::string describeDeviceFault(std::string_view description,
        std::span<const VkDeviceFaultAddressInfoEXT> addresses, std::span<const VkDeviceFaultVendorInfoEXT> vendor);

    /// The line a lost device's report says of the last checkpoint `stage` passed, or that the host
    /// no longer names it: the frame's or the picture's it was set in.
    std::string describePassedStage(VkPipelineStageFlagBits stage, const Checkpoint* checkpoint);

    /// What a device says of where its queue was when it was lost, or a wait for it ran out: the driver's own
    /// account (`VK_EXT_device_fault`), and the last checkpoint each stage passed, by NVIDIA's checkpoints or AMD's
    /// buffer markers, whichever the driver offers and the run asked for. The device's, and pinned, as every part
    /// the device makes is.
    class FaultReport
    {
    public:
        /// The entry points a driver offers for it, each null where it does not.
        struct EntryPoints
        {
            /// Null where the driver offers no `VK_EXT_device_fault`, or offers the extension without its feature.
            PFN_vkGetDeviceFaultInfoEXT mFaultInfo = nullptr;

            /// Null where the driver offers no `VK_NV_device_diagnostic_checkpoints`.
            PFN_vkCmdSetCheckpointNV mSetCheckpoint = nullptr;
            PFN_vkGetQueueCheckpointDataNV mCheckpointData = nullptr;

            /// Null where the driver offers no `VK_AMD_buffer_marker`, or the run asked for no markers.
            PFN_vkCmdWriteBufferMarkerAMD mWriteMarker = nullptr;
        };

        /// Made after the device's allocator, which AMD's markers are made on.
        ///
        /// @param uncachedMarkers whether the markers are in memory the device writes past its caches.
        FaultReport(const Device& device, const EntryPoints& entries, bool uncachedMarkers);
        ~FaultReport();

        FaultReport(const FaultReport&) = delete;
        FaultReport& operator=(const FaultReport&) = delete;
        FaultReport(FaultReport&&) = delete;
        FaultReport& operator=(FaultReport&&) = delete;

        /// Marks the queue's progress with `checkpoint`, which the queue reports as the last one each stage passed
        /// if the device is lost: NVIDIA's checkpoints or AMD's buffer markers, whichever the driver offers, where
        /// the run asked for them (`DeviceReport::mCheckpoints`, `mMarkers`). Nothing where it did not, or where
        /// the driver offers neither. On the one thread that records.
        void checkpoint(VkCommandBuffer commands, const Checkpoint* checkpoint) const
        {
            if (mEntries.mSetCheckpoint != nullptr)
                mEntries.mSetCheckpoint(commands, checkpoint);
            if (mMarkers != nullptr)
                writeMarker(commands, checkpoint);
        }

        /// What the device says about why it was lost, as lines for the message that reports it, and the last
        /// checkpoint each stage passed. Nothing of the first where the driver offers no `VK_EXT_device_fault`.
        /// After a loss and never before, because the extension forbids the question of a device that is still
        /// answering.
        std::string describeFault() const;

        /// The last checkpoint each stage of the queue passed by AMD's buffer markers, as lines for the report of
        /// a wait that ran out: words in host memory, which a device still answering can be asked, where NVIDIA's
        /// checkpoints are asked only of a lost one (`VUID-vkGetQueueCheckpointDataNV-queue-02025`). Nothing where
        /// the driver offers no markers.
        std::string describeMarkers() const;

        // Read by the tests and by nothing else.
        /// Whether the driver offers `VK_EXT_device_fault` with its feature, and so whether `describeFault` has
        /// anything to ask.
        bool canDescribeFault() const { return mEntries.mFaultInfo != nullptr; }

    private:
        /// The last checkpoint each stage of the queue passed, as lines for the fault report. Nothing where the
        /// driver offers neither checkpoints nor buffer markers.
        std::string describeCheckpoints() const;

        /// `BufferMarkers::mark`, out of line, where the markers' type is whole.
        void writeMarker(VkCommandBuffer commands, const Checkpoint* checkpoint) const;

        const Device& mDevice;
        EntryPoints mEntries;

        /// AMD's breadcrumbs, null where the driver offers no `VK_AMD_buffer_marker`.
        std::unique_ptr<BufferMarkers> mMarkers;
    };
}
