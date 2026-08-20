#ifndef OPENMW_COMPONENTS_RTX_VISIBILITYPASS_H
#define OPENMW_COMPONENTS_RTX_VISIBILITYPASS_H

#include <cstdint>
#include <filesystem>

#include <osg/Vec3f>

#include <vulkan/vulkan_core.h>

#include "shaders/visibility.h"

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;

    /// Constants for a pinhole camera looking from `origin` towards `target`.
    ///
    /// The world's up is +Z, as Morrowind has it. A camera standing where it is looking, or pointed
    /// straight up or straight down, has no basis; both throw `Error`. These arrive from a command
    /// line, so they are input rather than a contract, and the alternative to a message is a
    /// normalised zero vector quietly filling the image with NaN.
    Shaders::VisibilityConstants makeCamera(const osg::Vec3f& origin, const osg::Vec3f& target,
        float verticalFovDegrees, std::uint32_t width, std::uint32_t height, float far);

    /// One ray per pixel against the top-level structure, shaded by the geometric normal it hit.
    ///
    /// Everything it needs is pushed at record time — no descriptor pool, no set to allocate, and so
    /// nothing for it to allocate per frame either.
    class VisibilityPass
    {
    public:
        VisibilityPass(const Device& device, const std::filesystem::path& shaderDirectory);
        ~VisibilityPass();

        VisibilityPass(const VisibilityPass&) = delete;
        VisibilityPass& operator=(const VisibilityPass&) = delete;

        /// @param target must be in `VK_IMAGE_LAYOUT_GENERAL`.
        /// @param hitCount a storage buffer of one `uint32` the shader increments per hit.
        void record(VkCommandBuffer commands, VkAccelerationStructureKHR scene, const Image& target,
            const Buffer& hitCount, const Shaders::VisibilityConstants& constants) const;

    private:
        const Device& mDevice;
        VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        VkPipeline mPipeline = VK_NULL_HANDLE;
    };
}

#endif
