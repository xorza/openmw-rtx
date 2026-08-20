#ifndef OPENMW_COMPONENTS_RTX_SCENEBUFFERS_H
#define OPENMW_COMPONENTS_RTX_SCENEBUFFERS_H

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;
    class SceneDesc;

    /// The tables a shader reads at a hit: what the triangle was, and how it is shaded.
    ///
    /// Positions and indices are already on the GPU for the acceleration structure to be built from,
    /// but a hit needs the *attributes* — and the mesh, instance and material tables to find them
    /// through. Position fetch covered a normal; nothing covers a texture coordinate.
    class SceneBuffers
    {
    public:
        /// @param indices the buffer `SceneAcceleration` already built from, borrowed rather than
        ///        uploaded again. It must outlive this.
        SceneBuffers(const Device& device, CommandPool& pool, const SceneDesc& scene, VkBuffer indices);

        SceneBuffers(const SceneBuffers&) = delete;
        SceneBuffers& operator=(const SceneBuffers&) = delete;

        VkBuffer getNormals() const { return mNormals.getHandle(); }
        VkBuffer getTexCoords() const { return mTexCoords.getHandle(); }
        VkBuffer getIndices() const { return mIndices; }
        VkBuffer getMeshes() const { return mMeshes.getHandle(); }
        VkBuffer getInstances() const { return mInstances.getHandle(); }
        VkBuffer getMaterials() const { return mMaterials.getHandle(); }
        VkBuffer getLayers() const { return mLayers.getHandle(); }
        VkBuffer getMasks() const { return mMasks.getHandle(); }
        VkBuffer getLights() const { return mLights.getHandle(); }
        VkBuffer getWaves() const { return mWaves.getHandle(); }

        VkDeviceSize getBytes() const;

    private:
        Buffer mNormals;
        Buffer mTexCoords;
        VkBuffer mIndices = VK_NULL_HANDLE;
        Buffer mMeshes;
        Buffer mInstances;
        Buffer mMaterials;
        Buffer mLayers;
        Buffer mMasks;
        Buffer mLights;
        Buffer mWaves;
    };
}

#endif
