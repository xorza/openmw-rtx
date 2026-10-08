#include <array>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <volk.h>

#include <components/rtxvulkan/device/bindingtable.hpp>
#include <components/rtxvulkan/device/handles.hpp>
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

        /// **A module's bindings are held to its layout's, set by set**: the same type and count
        /// agree, a binding the layout does not declare, another type and another count each
        /// disagree and say which binding of which set, and what the layout declares past what the
        /// module reads is no disagreement. A set the layout does not name disagrees whatever the
        /// module reads in it. The census, which the layout adds itself, is not the pass's to state;
        /// an array of no length takes the layout's count.
        TEST(RtxPassBindingsTest, aModulesBindingsAreHeldToTheLayoutsTypeAndCountInEverySet)
        {
            const std::array pass{ declared(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
                declared(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4),
                declared(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC),
                declared(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) };
            const std::array textures{ declared(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8) };
            const BindingTable passTable(pass);
            const BindingTable textureTable(textures);
            SetTables layout{};
            layout[Shaders::SET_PASS] = &passTable;
            layout[Shaders::SET_TEXTURES] = &textureTable;

            const std::array agreeing{ bound(0, DescriptorKind::StorageImage),
                bound(1, DescriptorKind::SampledImage, 4), bound(2, DescriptorKind::StorageBuffer),
                bound(Shaders::BIND_CENSUS, DescriptorKind::StorageBuffer),
                bound(0, DescriptorKind::CombinedImageSampler, 0, Shaders::SET_TEXTURES) };
            EXPECT_EQ(bindingDisagreement(agreeing, layout), std::nullopt);

            const std::array unbounded{ bound(1, DescriptorKind::SampledImage, 0) };
            EXPECT_EQ(bindingDisagreement(unbounded, layout), std::nullopt);

            const std::array missing{ bound(5, DescriptorKind::StorageImage) };
            EXPECT_EQ(
                bindingDisagreement(missing, layout), "binding 5 of set 0 is in the module and not in the layout");

            const std::array retyped{ bound(0, DescriptorKind::SampledImage) };
            EXPECT_EQ(bindingDisagreement(retyped, layout),
                "binding 0 of set 0 is descriptor type " + std::to_string(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                    + " in the layout and another in the module");

            const std::array recounted{ bound(1, DescriptorKind::SampledImage, 2) };
            EXPECT_EQ(
                bindingDisagreement(recounted, layout), "binding 1 of set 0 is 4 in the layout and 2 in the module");

            // **The shared sets are held as the pass's is**: a storage image where the textures'
            // set samples, and a count the layout does not give, each named with its set.
            const std::array sharedRetyped{ bound(0, DescriptorKind::StorageImage, 1, Shaders::SET_TEXTURES) };
            EXPECT_EQ(bindingDisagreement(sharedRetyped, layout),
                "binding 0 of set 1 is descriptor type " + std::to_string(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    + " in the layout and another in the module");
            const std::array sharedRecounted{ bound(
                0, DescriptorKind::CombinedImageSampler, 2, Shaders::SET_TEXTURES) };
            EXPECT_EQ(bindingDisagreement(sharedRecounted, layout),
                "binding 0 of set 1 is 8 in the layout and 2 in the module");

            const std::array unnamed{ bound(0, DescriptorKind::StorageImage, 1, Shaders::SET_VOLUME) };
            EXPECT_EQ(bindingDisagreement(unnamed, layout), "the module reads set 3, which the layout does not name");
        }
    }
}
