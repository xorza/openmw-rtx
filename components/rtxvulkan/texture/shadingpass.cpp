#include "shadingpass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// The texture in, the sums out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADING_SUM_BINDINGS> sSumBindings{
            computeBinding(Shaders::SHADING_SUM_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::SHADING_SUM_BIND_SUMS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        };

        /// The sums in, the map out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADING_MAP_BINDINGS> sMapBindings{
            computeBinding(Shaders::SHADING_MAP_BIND_SUMS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            computeBinding(Shaders::SHADING_MAP_BIND_MAP, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };

        /// What both dispatches are told: the image's own size, and whether it punches through.
        Shaders::ShadingConstants constantsOf(const Image& source, const bool punchThrough)
        {
            return Shaders::ShadingConstants{
                .mWidth = source.getWidth(),
                .mHeight = source.getHeight(),
                .mPunchThrough = punchThrough ? 1u : 0u,
            };
        }
    }

    ShadingPass::ShadingPass(const Device& device)
        : mSum(device, sSumBindings, {}, "shadingsum.comp.spv", "shading sum")
        , mMap(device, sMapBindings, {}, "shadingmap.comp.spv", "shading map")
    {
    }

    void ShadingPass::recordSum(const VkCommandBuffer commands, const Image& source, const VkDescriptorBufferInfo& sums,
        const bool punchThrough) const
    {
        assert(sums.range == sSumBytes && "sums of another size than the card");

        DescriptorWrites summing(mSum);
        summing.image(Shaders::SHADING_SUM_BIND_SOURCE,
            source.describeSampled(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        summing.buffer(Shaders::SHADING_SUM_BIND_SUMS, sums);
        dispatch(commands, mSum, summing, constantsOf(source, punchThrough), Groups{ .mX = Shaders::SHADING_EXTENT });
    }

    void ShadingPass::recordMap(const VkCommandBuffer commands, const Image& source, const Image& map,
        const VkDescriptorBufferInfo& sums, const bool punchThrough) const
    {
        assert(sums.range == sSumBytes && "sums of another size than the card");

        DescriptorWrites mapping(mMap);
        mapping.buffer(Shaders::SHADING_MAP_BIND_SUMS, sums);
        mapping.image(Shaders::SHADING_MAP_BIND_MAP, map.describeStorage());
        dispatch(commands, mMap, mapping, constantsOf(source, punchThrough), Groups{});
    }
}
