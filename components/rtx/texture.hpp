#ifndef OPENMW_COMPONENTS_RTX_TEXTURE_H
#define OPENMW_COMPONENTS_RTX_TEXTURE_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "memory.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;

    /// Where one mip level sits in a texture's bytes, and how big it is.
    struct MipLevel
    {
        std::uint32_t mOffset = 0;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
    };

    /// A decoded texture, ready to upload and owning none of it.
    ///
    /// Deliberately not an `osg::Image`: this layer does not know OpenSceneGraph, and everything
    /// below is true of a block-compressed file read straight off disk. Morrowind's textures arrive
    /// as BC1 or BC2 with their mip chains already built, so uploading is a copy and never a
    /// conversion.
    struct TextureData
    {
        VkFormat mFormat = VK_FORMAT_UNDEFINED;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// Every level, back to back. The levels index into this.
        std::span<const std::byte> mBytes;
        std::span<const MipLevel> mLevels;
    };

    /// A sampled image on the GPU.
    class Texture
    {
    public:
        Texture(const Device& device, CommandPool& pool, const TextureData& data, const std::string& name);
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
        /// @param textures may be empty; the shader is told the count and does not index past it.
        TextureArray(const Device& device, std::vector<Texture>&& textures);
        ~TextureArray();

        TextureArray(const TextureArray&) = delete;
        TextureArray& operator=(const TextureArray&) = delete;

        VkDescriptorSetLayout getLayout() const { return mLayout; }
        VkDescriptorSet getSet() const { return mSet; }
        std::uint32_t getCount() const { return static_cast<std::uint32_t>(mTextures.size()); }
        VkDeviceSize getBytes() const;

    private:
        const Device& mDevice;
        std::vector<Texture> mTextures;
        VkSampler mSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout mLayout = VK_NULL_HANDLE;
        VkDescriptorPool mPool = VK_NULL_HANDLE;
        VkDescriptorSet mSet = VK_NULL_HANDLE;
    };
}

#endif
