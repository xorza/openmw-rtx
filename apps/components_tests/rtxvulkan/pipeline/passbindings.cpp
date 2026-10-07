#include <array>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <volk.h>

#include <components/rtxvulkan/pipeline/shadercode.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/shaders/shared/sets.h>

namespace Rtx
{
    namespace
    {
        VkDescriptorSetLayoutBinding declared(std::uint32_t binding, VkDescriptorType type, std::uint32_t count = 1)
        {
            return VkDescriptorSetLayoutBinding{ .binding = binding,
                .descriptorType = type,
                .descriptorCount = count,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr };
        }

        ModuleBinding bound(
            std::uint32_t binding, DescriptorKind kind, std::uint32_t count = 1, std::uint32_t set = Shaders::SET_PASS)
        {
            return ModuleBinding{ .mSet = set, .mBinding = binding, .mKind = kind, .mCount = count };
        }

        /// **A module's pass bindings are held to its layout's**: the same type and count agree, a
        /// binding the layout does not declare, another type and another count each disagree and
        /// say which binding, and what the layout declares past what the module reads is no
        /// disagreement. The census, which the layout adds itself, and every other set are not the
        /// pass's to state; an array of no length takes the layout's count.
        TEST(RtxPassBindingsTest, aModulesPassBindingsAreHeldToTheLayoutsTypeAndCount)
        {
            const std::array layout{ declared(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
                declared(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4),
                declared(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC),
                declared(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) };

            const std::array agreeing{ bound(0, DescriptorKind::StorageImage),
                bound(1, DescriptorKind::SampledImage, 4), bound(2, DescriptorKind::StorageBuffer),
                bound(Shaders::BIND_CENSUS, DescriptorKind::StorageBuffer),
                bound(0, DescriptorKind::AccelerationStructure, 1, Shaders::SET_PASS + 1) };
            EXPECT_EQ(passBindingDisagreement(agreeing, layout), std::nullopt);

            const std::array unbounded{ bound(1, DescriptorKind::SampledImage, 0) };
            EXPECT_EQ(passBindingDisagreement(unbounded, layout), std::nullopt);

            const std::array missing{ bound(5, DescriptorKind::StorageImage) };
            EXPECT_EQ(passBindingDisagreement(missing, layout), "binding 5 is in the module and not in the layout");

            const std::array retyped{ bound(0, DescriptorKind::SampledImage) };
            EXPECT_EQ(passBindingDisagreement(retyped, layout),
                "binding 0 is descriptor type " + std::to_string(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                    + " in the layout and another in the module");

            const std::array recounted{ bound(1, DescriptorKind::SampledImage, 2) };
            EXPECT_EQ(passBindingDisagreement(recounted, layout), "binding 1 is 4 in the layout and 2 in the module");
        }
    }
}
