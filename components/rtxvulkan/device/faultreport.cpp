#include "faultreport.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "buffermarkers.hpp"
#include "device.hpp"
#include "result.hpp"

namespace Rtx
{
    namespace
    {
        /// Which stage a checkpoint was reported for, for the handful a queue reports on and
        /// the number for the rest.
        std::string checkpointStageName(const VkPipelineStageFlagBits stage)
        {
            switch (stage)
            {
                case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
                    return "the top of the pipe";
                case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
                    return "the bottom of the pipe";
                case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:
                    return "the compute stage";
                case VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR:
                    return "the ray tracing stage";
                case VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR:
                    return "the structure build stage";
                case VK_PIPELINE_STAGE_TRANSFER_BIT:
                    return "the transfer stage";
                default:
                    return std::format("stage {:#x}", static_cast<std::uint32_t>(stage));
            }
        }

        /// What a faulting address was being used for, as the header spells it.
        std::string_view faultAddressTypeName(VkDeviceFaultAddressTypeEXT type)
        {
            switch (type)
            {
                case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:
                    return "a fault at no address";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
                    return "an invalid read";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
                    return "an invalid write";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
                    return "an invalid execute";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
                    return "an instruction pointer the driver could not place";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
                    return "an instruction pointer outside any shader";
                case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
                    return "the instruction that faulted";
                default:
                    return "an address of unrecognised type";
            }
        }
    }

    FaultReport::FaultReport(const Device& device, const EntryPoints& entries, const bool uncachedMarkers)
        : mDevice(device)
        , mEntries(entries)
        , mMarkers(entries.mWriteMarker == nullptr
                  ? nullptr
                  : std::make_unique<BufferMarkers>(device, entries.mWriteMarker, uncachedMarkers))
    {
    }

    FaultReport::~FaultReport() = default;

    void FaultReport::writeMarker(const VkCommandBuffer commands, const Checkpoint* const checkpoint) const
    {
        mMarkers->mark(commands, checkpoint);
    }

    std::string FaultReport::describeMarkers() const
    {
        if (mMarkers == nullptr)
            return {};

        const BufferMarkers::Passed passed = mMarkers->read();
        return describePassedStage(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, passed.mTop)
            + describePassedStage(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, passed.mBottom);
    }

    std::string FaultReport::describeCheckpoints() const
    {
        if (mMarkers != nullptr)
            return describeMarkers();

        if (mEntries.mCheckpointData == nullptr)
            return {};

        const std::vector<VkCheckpointDataNV> passed = enumerateVk<VkCheckpointDataNV>(
            "vkGetQueueCheckpointDataNV",
            [&](std::uint32_t* count, VkCheckpointDataNV* into) {
                mEntries.mCheckpointData(mDevice.getQueue(), count, into);
                return VK_SUCCESS;
            },
            VkCheckpointDataNV{ .sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV,
                .pNext = nullptr,
                .stage = VK_PIPELINE_STAGE_NONE,
                .pCheckpointMarker = nullptr });
        if (passed.empty())
            return "\nthe queue passed no checkpoint";

        std::string report;
        for (std::size_t at = 0; at < passed.size(); ++at)
        {
            const auto* checkpoint = static_cast<const Checkpoint*>(passed[at].pCheckpointMarker);
            if (checkpoint == nullptr)
                continue;

            report += describePassedStage(passed[at].stage, checkpoint);
        }

        return report;
    }

    std::string describeDeviceFault(const std::string_view description,
        const std::span<const VkDeviceFaultAddressInfoEXT> addresses,
        const std::span<const VkDeviceFaultVendorInfoEXT> vendor)
    {
        std::string report = "\ndevice fault: ";
        report += description.substr(0, description.find('\0'));
        for (const VkDeviceFaultAddressInfoEXT& address : addresses)
            report += std::format("\n  {} at {:#x}, known to within {:#x}", faultAddressTypeName(address.addressType),
                address.reportedAddress, address.addressPrecision);
        for (const VkDeviceFaultVendorInfoEXT& said : vendor)
            report += std::format("\n  {} (vendor code {:#x}, data {:#x})",
                std::string_view(said.description, std::char_traits<char>::length(said.description)),
                said.vendorFaultCode, said.vendorFaultData);
        return report;
    }

    std::string describePassedStage(const VkPipelineStageFlagBits stage, const Checkpoint* const checkpoint)
    {
        return checkpoint == nullptr
            ? std::format("\n  {} passed no checkpoint the host still names", checkpointStageName(stage))
            : std::format("\n  {} last passed `{}` of {} {}", checkpointStageName(stage), checkpoint->mName,
                  (checkpoint->mFrame & sPictureCheckpoint) != 0 ? "picture" : "frame",
                  checkpoint->mFrame & ~sPictureCheckpoint);
    }

    std::string FaultReport::describeFault() const
    {
        if (mEntries.mFaultInfo == nullptr)
            return describeCheckpoints();

        constexpr const char* sUnsaid = "\nthe driver would not say where the device faulted";

        VkDeviceFaultCountsEXT counts{ .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
            .pNext = nullptr,
            .addressInfoCount = 0,
            .vendorInfoCount = 0,
            .vendorBinarySize = 0 };
        if (mEntries.mFaultInfo(mDevice.getHandle(), &counts, nullptr) != VK_SUCCESS)
            return sUnsaid + describeCheckpoints();

        std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
        std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);

        // Nought, because the feature that provides the binary was not enabled.
        counts.vendorBinarySize = 0;

        VkDeviceFaultInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
            .pNext = nullptr,
            .description = {},
            .pAddressInfos = addresses.data(),
            .pVendorInfos = vendor.data(),
            .pVendorBinaryData = nullptr,
        };

        // `VK_INCOMPLETE` is the driver having more to say than the counts it gave a moment ago
        // allowed for, and what it did say is still worth reading.
        const VkResult result = mEntries.mFaultInfo(mDevice.getHandle(), &counts, &info);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
            return sUnsaid + describeCheckpoints();

        return describeDeviceFault(info.description, std::span(addresses).first(counts.addressInfoCount),
                   std::span(vendor).first(counts.vendorInfoCount))
            + describeCheckpoints();
    }
}
