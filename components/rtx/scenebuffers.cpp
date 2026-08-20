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
            // Zero where the material has no texture to read a mask out of, so that the shader's
            // comparison agrees with `Material::isCutout`, which is what decided whether traversal
            // would ever make it.
            return Shaders::GpuMaterial{
                .mDiffuse = material.mDiffuse,
                .mAlphaCutoff = material.isCutout() ? material.getAlphaCutoff() : 0.0f,
                .mLayerOffset = material.mLayerOffset,
                .mLayerCount = material.mLayerCount,
                .mDiffuseColour = material.mDiffuseColour,
            };
        }

        Shaders::GpuLight toGpu(const Light& light)
        {
            return Shaders::GpuLight{
                .mPosition = light.mPosition,
                .mIntensity = light.mIntensity,
                .mReach = light.mReach,
            };
        }

        Shaders::GpuLayer toGpu(const MaterialLayer& layer)
        {
            return Shaders::GpuLayer{
                .mDiffuse = layer.mDiffuse,
                .mMaskOffset = layer.mMaskOffset,
                .mMaskWidth = layer.mMaskWidth,
                .mMaskHeight = layer.mMaskHeight,
                .mDiffuseTransform = layer.mDiffuseTransform,
                .mMaskTransform = layer.mMaskTransform,
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
            .mAlphaCutoff = 0.0f,
            .mLayerOffset = 0,
            .mLayerCount = 0,
            .mDiffuseColour = osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f),
        });

        std::vector<Shaders::GpuInstance> instances;
        instances.reserve(scene.getInstances().size());
        for (const MeshInstance& instance : scene.getInstances())
            instances.push_back(Shaders::GpuInstance{
                .mMesh = instance.mMesh,
                .mMaterial = instance.mMaterial == sNoIndex ? sentinel : instance.mMaterial,
            });

        std::vector<Shaders::GpuLayer> layers;
        layers.reserve(scene.getLayers().size());
        for (const MaterialLayer& layer : scene.getLayers())
            layers.push_back(toGpu(layer));

        std::vector<Shaders::GpuLight> lights;
        lights.reserve(scene.getLights().size());
        for (const Light& light : scene.getLights())
            lights.push_back(toGpu(light));

        mNormals = uploadBuffer(device, pool, scene.getNormals(), sTableUsage);
        mTexCoords = uploadBuffer(device, pool, scene.getTexCoords(), sTableUsage);
        mMeshes = uploadBuffer(device, pool, std::span<const Shaders::GpuMesh>(meshes), sTableUsage);
        mInstances = uploadBuffer(device, pool, std::span<const Shaders::GpuInstance>(instances), sTableUsage);
        mMaterials = uploadBuffer(device, pool, std::span<const Shaders::GpuMaterial>(materials), sTableUsage);

        // A scene with no terrain in it still has to bind something: a descriptor may not be null,
        // and a zero-length buffer is not a thing Vulkan will make. One unread element each — and
        // the layer cannot be `constexpr`, because `osg::Vec4f` has no constexpr default.
        const Shaders::GpuLayer noLayer{};
        const Shaders::GpuLight noLight{};
        constexpr float noMask = 1.0f;

        mLayers = uploadBuffer(device, pool,
            layers.empty() ? std::span<const Shaders::GpuLayer>(&noLayer, 1)
                           : std::span<const Shaders::GpuLayer>(layers),
            sTableUsage);
        mMasks = uploadBuffer(device, pool,
            scene.getMasks().empty() ? std::span<const float>(&noMask, 1) : scene.getMasks(), sTableUsage);
        mLights = uploadBuffer(device, pool,
            lights.empty() ? std::span<const Shaders::GpuLight>(&noLight, 1)
                           : std::span<const Shaders::GpuLight>(lights),
            sTableUsage);

        device.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mMaterials.getHandle()), "materials");
        device.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mTexCoords.getHandle()), "uvs");
    }

    VkDeviceSize SceneBuffers::getBytes() const
    {
        // The indices are not counted here: they belong to the acceleration structure, which reports
        // its own size.
        return mNormals.getSize() + mTexCoords.getSize() + mMeshes.getSize() + mInstances.getSize()
            + mMaterials.getSize() + mLayers.getSize() + mMasks.getSize() + mLights.getSize();
    }
}
