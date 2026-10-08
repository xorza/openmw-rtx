#include <algorithm>
#include <array>
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
#include <components/rtxvulkan/device/handles.hpp>
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
        /// modules bind.
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
                    ADD_FAILURE() << "a kind no trace module binds";
                    return VK_DESCRIPTOR_TYPE_SAMPLER;
            }
        }

        /// `module`'s own bindings in every set it reads, as a layout would declare them, which
        /// lets a test read the module through tables that agree with it.
        struct ModuleTables
        {
            std::array<BindingTable, Shaders::SET_COUNT> mTables;
            SetTables mSets{};
        };

        ModuleTables tablesOf(const Device& device, const char* module)
        {
            const std::vector<ModuleBinding> bindings
                = readInterface(readSpirv(device.getShaderDirectory() / module)).mBindings;

            ModuleTables tables;
            for (std::uint32_t set = 0; set < Shaders::SET_COUNT; ++set)
            {
                std::vector<VkDescriptorSetLayoutBinding> declared;
                for (const ModuleBinding& bound : bindings)
                    if (bound.mSet == set && !(set == Shaders::SET_PASS && bound.mBinding == Shaders::BIND_CENSUS))
                        declared.push_back(VkDescriptorSetLayoutBinding{ .binding = bound.mBinding,
                            .descriptorType = typeOf(bound.mKind),
                            .descriptorCount = std::max(bound.mCount, 1u),
                            .stageFlags = VK_SHADER_STAGE_ALL,
                            .pImmutableSamplers = nullptr });
                std::ranges::sort(declared, {}, &VkDescriptorSetLayoutBinding::binding);
                tables.mTables[set] = BindingTable(declared);
            }
            return tables;
        }

        /// The tables of `tables` as a layout names them: every set, and the pass's own.
        SetTables named(const ModuleTables& tables)
        {
            SetTables sets{};
            for (std::uint32_t set = 0; set < Shaders::SET_COUNT; ++set)
                sets[set] = &tables.mTables[set];
            return sets;
        }

        /// **A file is read once however many stages name it**: a second ask hands back the chain
        /// the first made, which holds the whole file — its size in bytes and the magic number
        /// first — and **another file is a chain of its own**, which a later read moves none of.
        TEST_F(RtxShaderCodeTest, aFileIsReadOnceAndEveryStageNamingItSharesTheRead)
        {
            const Device& device = *mHarness.mDevice;
            ShaderCode code(device);
            const ModuleTables hitTables = tablesOf(device, "visibilityhit.rchit.spv");
            const ModuleTables raygenTables = tablesOf(device, "visibility.rgen.spv");

            const void* hit = code.stage("visibilityhit.rchit.spv", named(hitTables));
            const void* raygen = code.stage("visibility.rgen.spv", named(raygenTables));
            EXPECT_EQ(code.stage("visibilityhit.rchit.spv", named(hitTables)), hit);
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
            EXPECT_THROW(code.stage("there-is-no-such-shader.spv", SetTables{}), InputError);
        }

        /// **A stage whose module states its bindings otherwise than the layout ends the process,
        /// naming the module**, before the driver reads a resource as another: here every shared
        /// set as the module reads it, and a pass's set of nothing.
        TEST_F(RtxShaderCodeTest, aStageWhoseModuleDisagreesWithItsLayoutEndsTheProcessNamingIt)
        {
            const Device& device = *mHarness.mDevice;
            const ModuleTables tables = tablesOf(device, "visibility.rgen.spv");
            const BindingTable nothing;
            SetTables sets = named(tables);
            sets[Shaders::SET_PASS] = &nothing;
            Testing::expectDies(
                [&] {
                    ShaderCode code(device);
                    code.stage("visibility.rgen.spv", sets);
                },
                // `.+` for the number: gtest reads its own syntax on Windows, which has no bracket
                // class, and POSIX's elsewhere, which has no `\d`.
                "visibility.rgen.spv: binding .+ of set 0 is in the module and not in the layout");
        }
    }
}
