#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
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

        constexpr std::array<std::string_view, 1> sRaygen{ "visibility.rgen.spv" };

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

        /// `module`'s own bindings in every set it reads, as a layout would declare them, and a
        /// range and words that agree with its push block and its constants: what lets a test read
        /// the module through a statement that agrees with it.
        struct ModuleTables
        {
            std::array<BindingTable, Shaders::SET_COUNT> mTables;
            std::uint32_t mPushBytes = 0;
            std::vector<std::uint32_t> mWords;
        };

        ModuleTables tablesOf(const Device& device, const char* module)
        {
            const ModuleInterface interface = readInterface(readSpirv(device.getShaderDirectory() / module));
            const std::vector<ModuleBinding>& bindings = interface.mBindings;

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
            tables.mPushBytes = interface.mPushEnd.value_or(0);
            for (const ModuleSpecConstant& constant : interface.mSpecConstants)
                if (constant.mId != Shaders::SPEC_CENSUS_KERNEL)
                    tables.mWords.resize(std::max<std::size_t>(tables.mWords.size(), constant.mId + 1));
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

        /// **A file is read once however many stages name it**, where the code is made: a module
        /// named twice is one read, every ask hands back the chain that read made, which holds the
        /// whole file — its size in bytes and the magic number first — and **another file is a
        /// chain of its own**.
        TEST_F(RtxShaderCodeTest, aFileIsReadOnceAndEveryStageNamingItSharesTheRead)
        {
            const Device& device = *mHarness.mDevice;
            constexpr std::array<std::string_view, 3> modules{ "visibilityhit.rchit.spv", "visibility.rgen.spv",
                "visibilityhit.rchit.spv" };
            const ShaderCode code(device, modules);
            const ModuleTables hitTables = tablesOf(device, "visibilityhit.rchit.spv");
            const ModuleTables raygenTables = tablesOf(device, "visibility.rgen.spv");

            const void* hit
                = code.stage("visibilityhit.rchit.spv", named(hitTables), hitTables.mPushBytes, hitTables.mWords);
            const void* raygen
                = code.stage("visibility.rgen.spv", named(raygenTables), raygenTables.mPushBytes, raygenTables.mWords);
            EXPECT_EQ(
                code.stage("visibilityhit.rchit.spv", named(hitTables), hitTables.mPushBytes, hitTables.mWords), hit);
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

        /// A file that is not a module is refused where the code is made, and a stage of a module the
        /// code was not made to read is a contract broken.
        TEST_F(RtxShaderCodeTest, aFileThatIsNotSpirvIsRejectedRatherThanHandedToTheDriver)
        {
            constexpr std::array<std::string_view, 1> missing{ "there-is-no-such-shader.spv" };
            EXPECT_THROW(ShaderCode(*mHarness.mDevice, missing), InputError);

            const ShaderCode none(*mHarness.mDevice, {});
            Testing::expectDies([&] { static_cast<void>(none.stage("visibility.rgen.spv", SetTables{}, 0, {})); },
                "a stage of a module its shader code was not made to read");
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
                    const ShaderCode code(device, sRaygen);
                    static_cast<void>(code.stage("visibility.rgen.spv", sets, tables.mPushBytes, tables.mWords));
                },
                // `.+` for the number: gtest reads its own syntax on Windows, which has no bracket
                // class, and POSIX's elsewhere, which has no `\d`.
                "visibility.rgen.spv: binding .+ of set 0 is in the module and not in the layout");

            // **And a word list cut short**, which left the last constant at its GLSL default.
            const std::span<const std::uint32_t> cut(tables.mWords.data(), tables.mWords.size() - 1);
            Testing::expectDies(
                [&] {
                    const ShaderCode code(device, sRaygen);
                    static_cast<void>(code.stage("visibility.rgen.spv", named(tables), tables.mPushBytes, cut));
                },
                "visibility.rgen.spv: specialization constant .+ is past the .+ words the stage is handed");
        }
    }
}
