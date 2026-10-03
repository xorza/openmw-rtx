#include <cstdint>
#include <filesystem>
#include <utility>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/pipeline/shadercode.hpp>

namespace Rtx
{
    namespace
    {
        using RtxShaderCodeTest = Testing::DeviceTest;

        /// The code at the end of a stage's chain, past the name where the device names objects.
        const VkShaderModuleCreateInfo& codeOf(const Device& device, const void* stage)
        {
            const auto* head = static_cast<const VkBaseInStructure*>(stage);
            if (device.namesObjects())
            {
                EXPECT_EQ(head->sType, VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
                head = head->pNext;
            }
            EXPECT_EQ(head->sType, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
            EXPECT_EQ(head->pNext, nullptr);
            return *reinterpret_cast<const VkShaderModuleCreateInfo*>(head);
        }

        /// **A file is read once however many stages name it**: a second ask hands back the chain
        /// the first made, which holds the whole file — its size in bytes and the magic number
        /// first — and **another file is a chain of its own**, which a later read moves none of.
        TEST_F(RtxShaderCodeTest, aFileIsReadOnceAndEveryStageNamingItSharesTheRead)
        {
            const Device& device = *mHarness.mDevice;
            ShaderCode code(device);

            const void* hit = code.stage("visibilityhit.rchit.spv");
            const void* raygen = code.stage("visibility.rgen.spv");
            EXPECT_EQ(code.stage("visibilityhit.rchit.spv"), hit);
            EXPECT_NE(raygen, hit);

            for (const auto& [stage, file] :
                { std::pair{ hit, "visibilityhit.rchit.spv" }, std::pair{ raygen, "visibility.rgen.spv" } })
            {
                const VkShaderModuleCreateInfo& read = codeOf(device, stage);
                EXPECT_EQ(read.codeSize, std::filesystem::file_size(device.getShaderDirectory() / file)) << file;
                EXPECT_EQ(read.pCode[0], 0x07230203u) << file;
            }

            if (device.namesObjects())
            {
                EXPECT_STREQ(
                    static_cast<const VkDebugUtilsObjectNameInfoEXT*>(hit)->pObjectName, "visibilityhit.rchit.spv");
            }
        }

        TEST_F(RtxShaderCodeTest, aFileThatIsNotSpirvIsRejectedRatherThanHandedToTheDriver)
        {
            ShaderCode code(*mHarness.mDevice);
            EXPECT_THROW(code.stage("there-is-no-such-shader.spv"), InputError);
        }
    }
}
