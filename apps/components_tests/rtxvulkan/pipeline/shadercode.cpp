#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/bindingtable.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/pipeline/shadercode.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/shaders/shared/sets.h>
#include <components/rtxvulkan/spirv/spirvfile.hpp>
#include <components/rtxvulkan/spirv/spirvinterface.hpp>

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

        /// The descriptor type a layout declares a resource of `kind` as, for the kinds the trace's
        /// modules bind in their pass's set.
        VkDescriptorType typeOf(const DescriptorKind kind)
        {
            switch (kind)
            {
                case DescriptorKind::SampledImage:
                    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case DescriptorKind::CombinedImageSampler:
                    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                case DescriptorKind::StorageImage:
                    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                case DescriptorKind::UniformBuffer:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                case DescriptorKind::StorageBuffer:
                    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                case DescriptorKind::AccelerationStructure:
                    return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                default:
                    ADD_FAILURE() << "a kind no trace module binds in its pass's set";
                    return VK_DESCRIPTOR_TYPE_SAMPLER;
            }
        }

        /// `module`'s own pass bindings as a layout would declare them, which lets a test read the
        /// module through a table that agrees with it.
        BindingTable tableOf(const Device& device, const char* module)
        {
            const std::vector<ModuleBinding> bindings
                = readInterface(readSpirv(device.getShaderDirectory() / module)).mBindings;

            std::vector<VkDescriptorSetLayoutBinding> pass;
            for (const ModuleBinding& bound : bindings)
                if (bound.mSet == Shaders::SET_PASS && bound.mBinding != Shaders::BIND_CENSUS)
                    pass.push_back(VkDescriptorSetLayoutBinding{ .binding = bound.mBinding,
                        .descriptorType = typeOf(bound.mKind),
                        .descriptorCount = std::max(bound.mCount, 1u),
                        .stageFlags = VK_SHADER_STAGE_ALL,
                        .pImmutableSamplers = nullptr });
            std::ranges::sort(pass, {}, &VkDescriptorSetLayoutBinding::binding);
            return BindingTable(pass);
        }

        /// **A file is read once however many stages name it**: a second ask hands back the chain
        /// the first made, which holds the whole file — its size in bytes and the magic number
        /// first — and **another file is a chain of its own**, which a later read moves none of.
        TEST_F(RtxShaderCodeTest, aFileIsReadOnceAndEveryStageNamingItSharesTheRead)
        {
            const Device& device = *mHarness.mDevice;
            ShaderCode code(device);
            const BindingTable hitPass = tableOf(device, "visibilityhit.rchit.spv");
            const BindingTable raygenPass = tableOf(device, "visibility.rgen.spv");

            const void* hit = code.stage("visibilityhit.rchit.spv", hitPass);
            const void* raygen = code.stage("visibility.rgen.spv", raygenPass);
            EXPECT_EQ(code.stage("visibilityhit.rchit.spv", hitPass), hit);
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
            EXPECT_THROW(code.stage("there-is-no-such-shader.spv", BindingTable{}), InputError);
        }

        /// **A stage whose module states its pass's bindings otherwise than the layout ends the
        /// process, naming the module**, before the driver reads a resource as another.
        TEST_F(RtxShaderCodeTest, aStageWhoseModuleDisagreesWithItsLayoutEndsTheProcessNamingIt)
        {
            const Device& device = *mHarness.mDevice;
            Testing::expectDies(
                [&] {
                    ShaderCode code(device);
                    code.stage("visibility.rgen.spv", BindingTable{});
                },
                // `.+` for the number: gtest reads its own syntax on Windows, which has no bracket
                // class, and POSIX's elsewhere, which has no `\d`.
                "visibility.rgen.spv: binding .+ is in the module and not in the layout");
        }
    }
}
