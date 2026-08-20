#include "texture.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

#include "buffer.hpp"
#include "commands.hpp"
#include "device.hpp"
#include "error.hpp"

namespace Rtx
{
    namespace
    {
        /// How many textures a scene may hold.
        ///
        /// The descriptor array is sized once and bound for the run; a cell of Morrowind reaches a
        /// couple of hundred, and a worldspace will not reach this.
        constexpr std::uint32_t sMaxTextures = 4096;

        VkDescriptorSetLayout makeLayout(const Device& device, std::uint32_t count)
        {
            const VkDescriptorSetLayoutBinding binding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = std::max(count, 1u),
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            };

            // Partially bound because a scene with fewer textures than the array can hold leaves the
            // tail unwritten, and a shader that never indexes there must not be told it is an error.
            constexpr VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = 1,
                .pBindingFlags = &flags,
            };

            const VkDescriptorSetLayoutCreateInfo create{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = &bindingFlags,
                .bindingCount = 1,
                .pBindings = &binding,
            };

            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            checkVk(vkCreateDescriptorSetLayout(device.getHandle(), &create, nullptr, &layout),
                "vkCreateDescriptorSetLayout");
            return layout;
        }
    }

    Texture::Texture(const Device& device, CommandPool& pool, const TextureData& data, const std::string& name)
        : mDevice(device.getHandle())
        , mBytes(data.mBytes.size())
    {
        assert(!data.mLevels.empty());
        assert(data.mFormat != VK_FORMAT_UNDEFINED);

        const auto levels = static_cast<std::uint32_t>(data.mLevels.size());

        const VkImageCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = data.mFormat,
            .extent = { data.mWidth, data.mHeight, 1 },
            .mipLevels = levels,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        checkVk(vkCreateImage(mDevice, &create, nullptr, &mHandle), "vkCreateImage");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, mHandle, &requirements);
        mMemory = DeviceMemory(
            device, requirements.size, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
        checkVk(vkBindImageMemory(mDevice, mHandle, mMemory.getHandle(), 0), "vkBindImageMemory");

        const Buffer staging(device, data.mBytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.write(data.mBytes);

        // Every level in one submit: the levels are already contiguous in the source, so this is one
        // copy per level out of one buffer rather than one upload per level.
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(levels);
        for (std::uint32_t level = 0; level < levels; ++level)
            regions.push_back(VkBufferImageCopy{
                .bufferOffset = data.mLevels[level].mOffset,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
                .imageExtent = { data.mLevels[level].mWidth, data.mLevels[level].mHeight, 1 },
            });

        const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1 };

        pool.submitAndWait([&](VkCommandBuffer commands) {
            const auto barrier
                = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                      VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
                      const VkImageMemoryBarrier2 image{
                          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                          .srcStageMask = srcStage,
                          .srcAccessMask = srcAccess,
                          .dstStageMask = dstStage,
                          .dstAccessMask = dstAccess,
                          .oldLayout = from,
                          .newLayout = to,
                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                          .image = mHandle,
                          .subresourceRange = whole,
                      };
                      const VkDependencyInfo dependency{
                          .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                          .imageMemoryBarrierCount = 1,
                          .pImageMemoryBarriers = &image,
                      };
                      vkCmdPipelineBarrier2(commands, &dependency);
                  };

            barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            vkCmdCopyBufferToImage(commands, staging.getHandle(), mHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<std::uint32_t>(regions.size()), regions.data());

            barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });

        const VkImageViewCreateInfo view{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = mHandle,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = data.mFormat,
            .subresourceRange = whole,
        };
        checkVk(vkCreateImageView(mDevice, &view, nullptr, &mView), "vkCreateImageView");

        device.setName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(mHandle), name.c_str());
    }

    Texture::~Texture()
    {
        destroy();
    }

    Texture::Texture(Texture&& other) noexcept
        : mDevice(other.mDevice)
        , mHandle(std::exchange(other.mHandle, VK_NULL_HANDLE))
        , mView(std::exchange(other.mView, VK_NULL_HANDLE))
        , mMemory(std::move(other.mMemory))
        , mBytes(other.mBytes)
    {
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            mDevice = other.mDevice;
            mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
            mView = std::exchange(other.mView, VK_NULL_HANDLE);
            mMemory = std::move(other.mMemory);
            mBytes = other.mBytes;
        }
        return *this;
    }

    void Texture::destroy()
    {
        if (mView != VK_NULL_HANDLE)
            vkDestroyImageView(mDevice, mView, nullptr);
        if (mHandle != VK_NULL_HANDLE)
            vkDestroyImage(mDevice, mHandle, nullptr);
        mView = VK_NULL_HANDLE;
        mHandle = VK_NULL_HANDLE;
        mMemory = DeviceMemory();
    }

    TextureArray::TextureArray(const Device& device, std::vector<Texture>&& textures)
        : mDevice(device)
        , mTextures(std::move(textures))
    {
        if (mTextures.size() > sMaxTextures)
            throw Error("a scene with " + std::to_string(mTextures.size()) + " textures is past the "
                + std::to_string(sMaxTextures) + " this array holds");

        const VkSamplerCreateInfo sampler{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            // Morrowind's textures tile, and a great many of them rely on it.
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            // Off, and not an oversight: every fetch names its own level, and anisotropic filtering
            // only applies to the implicit and gradient forms. A cone is isotropic by construction.
            .anisotropyEnable = VK_FALSE,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        checkVk(vkCreateSampler(device.getHandle(), &sampler, nullptr, &mSampler), "vkCreateSampler");

        const auto count = static_cast<std::uint32_t>(std::max<std::size_t>(mTextures.size(), 1));
        mLayout = makeLayout(device, count);

        const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count };
        const VkDescriptorPoolCreateInfo pool{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &size,
        };
        checkVk(vkCreateDescriptorPool(device.getHandle(), &pool, nullptr, &mPool), "vkCreateDescriptorPool");

        const VkDescriptorSetAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = mPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mLayout,
        };
        checkVk(vkAllocateDescriptorSets(device.getHandle(), &allocate, &mSet), "vkAllocateDescriptorSets");

        if (mTextures.empty())
            return;

        std::vector<VkDescriptorImageInfo> images;
        images.reserve(mTextures.size());
        for (const Texture& texture : mTextures)
            images.push_back(VkDescriptorImageInfo{
                .sampler = mSampler,
                .imageView = texture.getView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            });

        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mSet,
            .dstBinding = 0,
            .descriptorCount = static_cast<std::uint32_t>(images.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = images.data(),
        };
        vkUpdateDescriptorSets(device.getHandle(), 1, &write, 0, nullptr);
    }

    TextureArray::~TextureArray()
    {
        if (mPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(mDevice.getHandle(), mPool, nullptr);
        if (mLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mDevice.getHandle(), mLayout, nullptr);
        if (mSampler != VK_NULL_HANDLE)
            vkDestroySampler(mDevice.getHandle(), mSampler, nullptr);
    }

    VkDeviceSize TextureArray::getBytes() const
    {
        VkDeviceSize total = 0;
        for (const Texture& texture : mTextures)
            total += texture.getBytes();
        return total;
    }
}
