#include <array>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/device.hpp>

namespace Rtx
{
    namespace
    {
        /// **A lost device's report, line for line**: what the driver said and the addresses and the
        /// vendor's codes it gave, each address by what it was used for; and each stage's checkpoint,
        /// a frame's or a picture's, or none the host still names.
        TEST(RtxFaultReportTest, aFaultAndItsCheckpointsAreSaidLineForLine)
        {
            const std::array addresses{
                VkDeviceFaultAddressInfoEXT{ .addressType = VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT,
                    .reportedAddress = 0x7f0012340000,
                    .addressPrecision = 0x1000 },
                VkDeviceFaultAddressInfoEXT{
                    .addressType = VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT,
                    .reportedAddress = 0x40,
                    .addressPrecision = 0x4 },
            };
            std::array<VkDeviceFaultVendorInfoEXT, 1> vendor{};
            std::strcpy(vendor[0].description, "page fault in a shader");
            vendor[0].vendorFaultCode = 0x1c;
            vendor[0].vendorFaultData = 0xdead;

            EXPECT_EQ(describeDeviceFault("MMU fault", addresses, vendor),
                "\ndevice fault: MMU fault"
                "\n  an invalid read at 0x7f0012340000, known to within 0x1000"
                "\n  an instruction pointer outside any shader at 0x40, known to within 0x4"
                "\n  page fault in a shader (vendor code 0x1c, data 0xdead)");
            EXPECT_EQ(describeDeviceFault(std::string_view("short\0ends here", 15), {}, {}), "\ndevice fault: short")
                << "the driver's text ends at its terminator";

            const Checkpoint frame{ "tlas", 83 };
            const Checkpoint picture{ "shade", sPictureCheckpoint | 4 };
            EXPECT_EQ(describePassedStage(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, &frame),
                "\n  the top of the pipe last passed `tlas` of frame 83");
            EXPECT_EQ(describePassedStage(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, &picture),
                "\n  the bottom of the pipe last passed `shade` of picture 4");
            EXPECT_EQ(describePassedStage(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, nullptr),
                "\n  the top of the pipe passed no checkpoint the host still names");
        }
    }
}
