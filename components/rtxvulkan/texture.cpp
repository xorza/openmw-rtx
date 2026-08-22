#include "texture.hpp"

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

#include <components/rtx/error.hpp>
#include <components/rtx/shaders/scene.h>

#include "buffer.hpp"
#include "commands.hpp"
#include "device.hpp"
#include "result.hpp"

namespace Rtx
{
    namespace
    {
        /// How many textures a scene may hold.
        ///
        /// The descriptor array is sized once and bound for the run; a cell of Morrowind reaches a
        /// couple of hundred, and a worldspace will not reach this.
        constexpr std::uint32_t sMaxTextures = 4096;

        /// The one place a `TextureFormat` becomes Vulkan's.
        ///
        /// Every case is sRGB, and `TextureFormat` says why: the files hold display-encoded bytes
        /// and the hardware converts them in the filter, which is what hands the shader linear
        /// values for free.
        VkFormat toVulkanFormat(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat::Bc1RgbaSrgb:
                    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                case TextureFormat::Bc2Srgb:
                    return VK_FORMAT_BC2_SRGB_BLOCK;
                case TextureFormat::Bc3Srgb:
                    return VK_FORMAT_BC3_SRGB_BLOCK;
                case TextureFormat::Rgba8Unorm:
                    return VK_FORMAT_R8G8B8A8_UNORM;
            }

            // Unreachable for any value of the enumeration; a new one that forgets a case lands
            // here rather than creating an image with a format nobody chose.
            throw Error("unknown texture format");
        }

        /// The layout every array declares, which is the same layout whatever the scene holds.
        ///
        /// **Sized to the maximum and not to the scene, because a pipeline outlives a cell.** Two
        /// set layouts are compatible only where they are identically defined, so a layout that
        /// counted the scene's textures made every cell's array incompatible with the pipeline
        /// layout built from the last one's — and a renderer that keeps its pass across scenes, as
        /// this one does because building one compiles a shader, would bind a set the pipeline
        /// cannot accept. The count moves to the allocation, where it costs what the scene actually
        /// uses.
        VkDescriptorSetLayout makeLayout(const Device& device)
        {
            const VkDescriptorSetLayoutBinding binding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = sMaxTextures,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            };

            // Partially bound because a scene with fewer textures than the array can hold leaves the
            // tail unwritten, and a shader that never indexes there must not be told it is an error.
            // Variable count is what keeps the declared maximum from being what gets allocated.
            constexpr VkDescriptorBindingFlags flags
                = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
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

    Texture::Texture(const Device& device, CommandPool& pool, const TextureData& data, std::string_view name)
        : mDevice(device.getHandle())
        , mBytes(data.mBytes.size())
    {
        assert(!data.mLevels.empty());

        const VkFormat format = toVulkanFormat(data.mFormat);
        const auto levels = static_cast<std::uint32_t>(data.mLevels.size());

        const VkImageCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
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
            .format = format,
            .subresourceRange = whole,
        };
        checkVk(vkCreateImageView(mDevice, &view, nullptr, &mView), "vkCreateImageView");

        device.setName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(mHandle), name);
        device.setName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(mView), name);
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

    namespace
    {
        std::vector<Texture> uploadAll(const Device& device, CommandPool& pool, std::span<const TextureData> textures)
        {
            std::vector<Texture> uploaded;
            uploaded.reserve(textures.size());
            for (const TextureData& texture : textures)
                uploaded.emplace_back(device, pool, texture, texture.mName);

            return uploaded;
        }

        /// Every texture's shading map end to end, and a neutral one wherever there is no estimate.
        ///
        /// **A missing map has to be neutral rather than absent.** A material whose texture would
        /// not load still indexes this buffer, and reading whatever happened to be at that offset is
        /// how the reference implementation came to divide every untextured surface by two.
        constexpr std::size_t sShadingCells = std::size_t{ Shaders::SHADING_EXTENT } * Shaders::SHADING_EXTENT;

        /// Appends each description's shading map to `values`, a stand-in where it has none.
        void gatherShading(std::span<const TextureData> textures, std::vector<float>& values)
        {
            for (const TextureData& texture : textures)
            {
                const std::span<const float> map = texture.mShading;
                if (map.empty())
                {
                    values.insert(values.end(), sShadingCells, 1.0f);
                    continue;
                }

                assert(map.size() == sShadingCells);
                values.insert(values.end(), map.begin(), map.end());
            }
        }
    }

    TextureArray::TextureArray(const Device& device, CommandPool& pool, std::span<const TextureData> textures)
        : mDevice(device)
        , mTextures(uploadAll(device, pool, textures))
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

        // **Allocated at the maximum the layout declares, not at what this scene brought.** Sizing
        // the set to the cell is what made a texture arriving mean a new set, a new pool and every
        // image uploaded again; four thousand descriptors is a few hundred kilobytes of pool and it
        // is paid once. `extend` then only ever writes the range that is new.
        constexpr std::uint32_t count = sMaxTextures;
        mLayout = makeLayout(device);

        const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count };
        const VkDescriptorPoolCreateInfo describePool{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &size,
        };
        checkVk(vkCreateDescriptorPool(device.getHandle(), &describePool, nullptr, &mPool), "vkCreateDescriptorPool");

        // What the layout left open: the array is declared at its maximum and allocated at the
        // scene's, so the descriptors paid for are the ones a cell put in it.
        const VkDescriptorSetVariableDescriptorCountAllocateInfo variable{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
            .descriptorSetCount = 1,
            .pDescriptorCounts = &count,
        };
        const VkDescriptorSetAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = &variable,
            .descriptorPool = mPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mLayout,
        };
        checkVk(vkAllocateDescriptorSets(device.getHandle(), &allocate, &mSet), "vkAllocateDescriptorSets");

        gatherShading(textures, mShadingValues);
        reshade(0);
        describeFrom(0);
    }

    void TextureArray::extend(CommandPool& pool, std::span<const TextureData> arrived)
    {
        if (arrived.empty())
            return;

        const std::size_t from = mTextures.size();
        if (from + arrived.size() > sMaxTextures)
            throw Error("a scene with " + std::to_string(from + arrived.size()) + " textures is past the "
                + std::to_string(sMaxTextures) + " this array holds");

        mTextures.reserve(from + arrived.size());
        for (std::size_t at = 0; at < arrived.size(); ++at)
            mTextures.emplace_back(mDevice, pool, arrived[at], "texture " + std::to_string(from + at));

        gatherShading(arrived, mShadingValues);
        reshade(from);
        describeFrom(from);
    }

    void TextureArray::describeFrom(std::size_t from)
    {
        if (from >= mTextures.size())
            return;

        std::vector<VkDescriptorImageInfo> images;
        images.reserve(mTextures.size() - from);
        for (std::size_t at = from; at < mTextures.size(); ++at)
            images.push_back(VkDescriptorImageInfo{
                .sampler = mSampler,
                .imageView = mTextures[at].getView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            });

        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mSet,
            .dstBinding = 0,
            .dstArrayElement = static_cast<std::uint32_t>(from),
            .descriptorCount = static_cast<std::uint32_t>(images.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = images.data(),
        };
        vkUpdateDescriptorSets(mDevice.getHandle(), 1, &write, 0, nullptr);
    }

    void TextureArray::reshade(std::size_t from)
    {
        // One texture's worth even for a scene with none: a buffer of nothing is not a legal thing
        // to make, and the descriptor is bound either way.
        if (mShadingValues.empty())
            mShadingValues.assign(sShadingCells, 1.0f);

        const std::span<const float> values(mShadingValues);

        // **Grown in blocks, and only then rewritten whole.** A buffer sized exactly to the scene
        // would be made again for every texture that arrives, which is the spike this exists to
        // remove; a block of slack turns that into one write of the tail, most of the time.
        if (mShading.getSize() < values.size_bytes())
        {
            constexpr std::size_t slack = 128 * sShadingCells;
            const std::size_t room = ((values.size() + slack - 1) / slack) * slack;

            mShading = HostBuffer(mDevice, room * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            mShading.write(values);
            return;
        }

        mShading.writeAt(from * sShadingCells * sizeof(float), values.subspan(from * sShadingCells));
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
