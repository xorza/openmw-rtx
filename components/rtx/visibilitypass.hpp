#ifndef OPENMW_COMPONENTS_RTX_VISIBILITYPASS_H
#define OPENMW_COMPONENTS_RTX_VISIBILITYPASS_H

#include <cstdint>
#include <filesystem>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"
#include "shaders/visibility.h"

namespace Rtx
{
    class CommandPool;
    class Device;
    class Image;
    class SceneBuffers;

    /// What a trace reads about the world, as against the camera that looks at it.
    struct VisibilityInputs
    {
        VkAccelerationStructureKHR mScene = VK_NULL_HANDLE;
        const SceneBuffers* mBuffers = nullptr;

        /// The bindless texture array's set, bound once and not pushed.
        VkDescriptorSet mTextures = VK_NULL_HANDLE;
    };

    /// One ray per pixel against the top-level structure, shaded by the geometric normal it hit.
    ///
    /// Everything it needs is pushed at record time — no descriptor pool, no set to allocate, and so
    /// nothing for it to allocate per frame either.
    class VisibilityPass
    {
    public:
        /// @param pool used once, to get the blue-noise tile onto the device. The pass owns the
        ///        tile because it belongs to the sampler and not to the scene or the camera: it is
        ///        the same numbers whatever is being looked at.
        /// @param textureLayout the layout of the bindless array this will be handed at record
        ///        time. Needed here because a pipeline layout names every set it will ever see.
        VisibilityPass(const Device& device, CommandPool& pool, const std::filesystem::path& shaderDirectory,
            VkDescriptorSetLayout textureLayout);
        ~VisibilityPass();

        VisibilityPass(const VisibilityPass&) = delete;
        VisibilityPass& operator=(const VisibilityPass&) = delete;

        /// @param target must be in `VK_IMAGE_LAYOUT_GENERAL`.
        /// @param history a float image at least as large as `target`, in `VK_IMAGE_LAYOUT_GENERAL`,
        ///        holding the running sum when `mAccumulate` is set. Written every frame either way,
        ///        so its contents are undefined rather than preserved when it is not.
        /// @param hitCount a storage buffer of one `uint32` the shader increments per hit.
        void record(VkCommandBuffer commands, const VisibilityInputs& inputs, const Image& target, const Image& history,
            const Buffer& hitCount, const Shaders::VisibilityConstants& constants) const;

    private:
        const Device& mDevice;
        Buffer mBlueNoise;
        VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout mTextureLayout = VK_NULL_HANDLE;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        VkPipeline mPipeline = VK_NULL_HANDLE;
    };
}

#endif
