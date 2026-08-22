#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/texturedata.hpp>

#include "buffer.hpp"
#include "hostbuffer.hpp"
#include "memory.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;

    /// A sampled image on the GPU.
    class Texture
    {
    public:
        /// A slot with nothing in it yet, which is what the array holds while it is being filled.
        Texture() = default;

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

        /// Writes each of `arrived` into the slot it names, leaving every other texture alone.
        ///
        /// **This is why the set is allocated at the maximum rather than at the scene's count.**
        /// A cell arriving, or an actor walking into view with a body texture nobody has worn yet,
        /// used to mean the whole array made again — 327 images re-uploaded, measured at 150 to 225
        /// milliseconds, against 12 for every acceleration structure in the scene.
        ///
        /// **By slot and not by appending**, because a slot a departing cell freed is taken over
        /// wherever it sits. A slot at the end grows the array; one inside it replaces what was
        /// there, and the image that was there goes when it is replaced and not when it was freed —
        /// so no descriptor ever names an image that has been destroyed.
        void write(CommandPool& pool, std::span<const TextureData> arrived);

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
        /// Writes the descriptors for the slots `arrived` names.
        void describe(std::span<const TextureData> arrived);

        /// Writes the shading of the slots `arrived` names, growing the buffer first if it must.
        void reshade(std::span<const TextureData> arrived);

        /// Grows the shading buffer to hold `mShadingValues`, rewriting it whole. True where it did,
        /// which is what tells a caller its own write has already happened.
        bool growShading();

        /// Makes room for a slot at the end of the array, and refuses one past what it holds.
        void reserveSlot(std::uint32_t slot);

        const Device& mDevice;

        /// Indexed by slot. A slot the scene has freed keeps the image it had until something takes
        /// it over, which is what keeps its descriptor pointing at something that exists.
        std::vector<Texture> mTextures;

        /// Every texture's shading map, host side, so growing the buffer does not have to ask the
        /// descriptions for maps it has already uploaded. A cell's worth is a megabyte or so.
        std::vector<float> mShadingValues;
        HostBuffer mShading;
        VkSampler mSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout mLayout = VK_NULL_HANDLE;
        VkDescriptorPool mPool = VK_NULL_HANDLE;
        VkDescriptorSet mSet = VK_NULL_HANDLE;
    };
}
