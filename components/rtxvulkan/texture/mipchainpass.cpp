#include "mipchainpass.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        /// The first level read, the chain's counter, and a level written each.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::MIPCHAIN_BINDINGS> sBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::MIPCHAIN_BINDINGS> bindings
                = computeBindings<Shaders::MIPCHAIN_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            bindings[Shaders::MIPCHAIN_BIND_COUNTER]
                = computeBinding(Shaders::MIPCHAIN_BIND_COUNTER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            return bindings;
        }();
    }

    MipChainPass::MipChainPass(const Device& device)
        : mPipeline(device, sBindings, {}, "mipchain.comp.spv", "mip chain")
        , mCounters(Buffer::deviceLocal(device, Shaders::MIPCHAIN_CHAINS_MOST * Shaders::MIPCHAIN_COUNTER_STRIDE,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "mip chain counters"))
    {
        device.getPool().submitAndWait([&](VkCommandBuffer commands) {
            mCounters.clear(commands);
            mCounters.transition(commands, Use::sBufferClearWrite, Use::sBufferComputeReadWrite);
        });
    }

    void MipChainPass::record(const VkCommandBuffer commands, const std::span<const Image* const> images) const
    {
        assert(images.size() <= Shaders::MIPCHAIN_CHAINS_MOST && "more chains than the record has counters");

        // The first level written is read, every level is written, and the counters are ordered after the last record's
        // workgroups that put them back.
        Barriers opened(commands);
        for (const Image* image : images)
            image->addTransition(opened, Use::sComputeWrite, Use::sComputeReadWrite);
        opened.add(mCounters.describeBarrier(Use::sBufferComputeReadWrite, Use::sBufferComputeReadWrite));
        opened.flush();

        for (std::uint32_t chain = 0; chain < images.size(); ++chain)
        {
            const Image& image = *images[chain];
            const std::uint32_t side = image.getWidth();
            assert(!image.isEmpty() && "a chain built on an image nobody made");
            assert(image.getDepthAt(0) == 1 && "a volume's chain is uploaded rather than built");
            assert(image.getHeight() == side && "a chain of an image that is not square");
            assert(std::has_single_bit(side) && "a chain of a side that does not halve exactly");
            assert(image.getMipLevels() == static_cast<std::uint32_t>(std::bit_width(side))
                && "a chain that does not end at one texel");
            assert(image.getMipLevels() - 1 <= Shaders::MIPCHAIN_LEVELS_MOST && "a chain past the downsampler's");
            assert((image.getUsage() & VK_IMAGE_USAGE_STORAGE_BIT) != 0 && "a chain written through no storage view");

            const std::uint32_t levels = image.getMipLevels() - 1;
            if (levels == 0)
                continue;

            DescriptorWrites writes(mPipeline);
            writes.image(Shaders::MIPCHAIN_BIND_SOURCE, image.describeStorage(0));
            writes.buffer(Shaders::MIPCHAIN_BIND_COUNTER,
                mCounters.describe(VkDeviceSize{ chain } * Shaders::MIPCHAIN_COUNTER_STRIDE, sizeof(std::uint32_t)));
            // A binding past the chain's last level names that level, which the shader never reaches.
            for (std::uint32_t written = 0; written < Shaders::MIPCHAIN_LEVELS_MOST; ++written)
                writes.image(
                    Shaders::MIPCHAIN_BIND_LEVEL_0 + written, image.describeStorage(std::min(written + 1, levels)));

            const Groups groups = Groups::covering(side, side, Shaders::MIPCHAIN_TILE);
            const Shaders::MipChainConstants constants{
                .mSide = side,
                .mLevels = levels,
                .mWorkGroups = groups.mX * groups.mY,
                .mEncoded = withoutCurve(image.getFormat()) != image.getFormat() ? 1u : 0u,
            };
            dispatch(commands, mPipeline, writes, constants, groups);
        }

        // Into the scope every sampled image rests in: the chains built here, the wave and ripple tiles and a ground's
        // composite, are sampled in launches, the trace's and the fog scatter's.
        Barriers sampled(commands);
        for (const Image* image : images)
            image->addTransition(sampled, Use::sComputeReadWrite, Use::sShaderSample);
        sampled.flush();
    }
}
