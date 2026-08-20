#include "scenebuffers.hpp"

#include <vector>

#include "commands.hpp"
#include "device.hpp"
#include "scenedesc.hpp"
#include "shaders/scene.h"

namespace Rtx
{
    namespace
    {
        constexpr VkBufferUsageFlags sTableUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        Shaders::GpuMaterial toGpu(const Material& material)
        {
            std::uint32_t alpha = Shaders::ALPHA_OPAQUE;
            if (material.mAlphaMode == AlphaMode::Cutout)
                alpha = Shaders::ALPHA_CUTOUT;
            else if (material.mAlphaMode == AlphaMode::Blend)
                alpha = Shaders::ALPHA_BLEND;

            return Shaders::GpuMaterial{
                .mDiffuse = material.mDiffuse,
                .mAlphaMode = alpha,
                .mAlphaRef = material.mAlphaRef,
                .mDiffuseColour = material.mDiffuseColour,
            };
        }
    }

    SceneBuffers::SceneBuffers(const Device& device, CommandPool& pool, const SceneDesc& scene, VkBuffer indices)
        : mIndices(indices)
    {
        std::vector<Shaders::GpuMesh> meshes;
        meshes.reserve(scene.getMeshes().size());
        for (const MeshRange& mesh : scene.getMeshes())
            meshes.push_back(
                Shaders::GpuMesh{ .mVertexOffset = mesh.mVertexOffset, .mIndexOffset = mesh.mIndexOffset });

        std::vector<Shaders::GpuMaterial> materials;
        materials.reserve(scene.getMaterials().size() + 1);
        for (const Material& material : scene.getMaterials())
            materials.push_back(toGpu(material));

        // A drawable with no state set has no material, and `sNoIndex` is not somewhere the shader
        // can be allowed to look. One untextured entry at the end costs less than a branch per hit,
        // and every instance that had nothing points at it.
        const auto sentinel = static_cast<std::uint32_t>(materials.size());
        materials.push_back(Shaders::GpuMaterial{
            .mDiffuse = Shaders::NO_TEXTURE,
            .mAlphaMode = Shaders::ALPHA_OPAQUE,
            .mAlphaRef = 0.0f,
            .mDiffuseColour = osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f),
        });

        std::vector<Shaders::GpuInstance> instances;
        instances.reserve(scene.getInstances().size());
        for (const MeshInstance& instance : scene.getInstances())
            instances.push_back(Shaders::GpuInstance{
                .mMesh = instance.mMesh,
                .mMaterial = instance.mMaterial == sNoIndex ? sentinel : instance.mMaterial,
            });

        mNormals = uploadBuffer(device, pool, scene.getNormals(), sTableUsage);
        mTexCoords = uploadBuffer(device, pool, scene.getTexCoords(), sTableUsage);
        mMeshes = uploadBuffer(device, pool, std::span<const Shaders::GpuMesh>(meshes), sTableUsage);
        mInstances = uploadBuffer(device, pool, std::span<const Shaders::GpuInstance>(instances), sTableUsage);
        mMaterials = uploadBuffer(device, pool, std::span<const Shaders::GpuMaterial>(materials), sTableUsage);

        device.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mMaterials.getHandle()), "materials");
        device.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mTexCoords.getHandle()), "uvs");
    }

    VkDeviceSize SceneBuffers::getBytes() const
    {
        // The indices are not counted here: they belong to the acceleration structure, which reports
        // its own size.
        return mNormals.getSize() + mTexCoords.getSize() + mMeshes.getSize() + mInstances.getSize()
            + mMaterials.getSize();
    }
}
