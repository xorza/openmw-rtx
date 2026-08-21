#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/texturedata.hpp>

#include "buffer.hpp"
#include "memory.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;

    /// A sampled image on the GPU.
    class Texture
    {
    public:
        Texture(const Device& device, CommandPool& pool, const TextureData& data, std::string_view name);
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;

        VkImageView getView() const { return mView; }
        /// The size of the data uploaded, which for a block-compressed image is what it occupies.
        VkDeviceSize getBytes() const { return mBytes; }

    private:
        void destroy();

        VkDevice mDevice = VK_NULL_HANDLE;
        VkImage mHandle = VK_NULL_HANDLE;
        VkImageView mView = VK_NULL_HANDLE;
        DeviceMemory mMemory;
        VkDeviceSize mBytes = 0;
    };

    /// Every texture a scene uses, in one descriptor array a shader indexes by material.
    ///
    /// A separate set from the per-frame one: this is written once and bound for the run, while the
    /// other is pushed every frame. A bindless array cannot be a push descriptor anyway — there is
    /// no pushing four thousand of them per frame.
    class TextureArray
    {
    public:
        /// Uploads every description, in the order given, so a material's texture index is an index
        /// into this. May be empty; the shader is told the count and does not index past it.
        TextureArray(const Device& device, CommandPool& pool, std::span<const TextureData> textures);
        ~TextureArray();

        TextureArray(const TextureArray&) = delete;
        TextureArray& operator=(const TextureArray&) = delete;

        VkDescriptorSetLayout getLayout() const { return mLayout; }
        VkDescriptorSet getSet() const { return mSet; }

        /// Every texture's shading map, back to back, `SHADING_EXTENT` squared floats apiece.
        ///
        /// **A buffer and not a second bindless array.** The maps are a thousand floats each and
        /// read once per hit, so a manual bilinear out of a buffer costs four loads against one
        /// sample — and it keeps them out of the array the cone's mip selection measures, which is
        /// where interleaving them cost the reference implementation every grazing mip in the frame.
        VkBuffer getShading() const { return mShading.getHandle(); }
        std::uint32_t getCount() const { return static_cast<std::uint32_t>(mTextures.size()); }
        VkDeviceSize getBytes() const;

    private:
        const Device& mDevice;
        std::vector<Texture> mTextures;
        Buffer mShading;
        VkSampler mSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout mLayout = VK_NULL_HANDLE;
        VkDescriptorPool mPool = VK_NULL_HANDLE;
        VkDescriptorSet mSet = VK_NULL_HANDLE;
    };
}
