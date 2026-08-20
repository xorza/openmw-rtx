#include "sceneacceleration.hpp"

#include <cassert>
#include <cstring>
#include <span>

#include "commands.hpp"
#include "device.hpp"
#include "error.hpp"
#include "scenedesc.hpp"
#include "shaders/scene.h"

namespace Rtx
{
    namespace
    {
        /// `VkAccelerationStructureCreateInfoKHR::offset` must be a multiple of this.
        constexpr VkDeviceSize sStructureAlignment = 256;

        VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        // Storage as well as build input, because the shader reads the indices back at a hit and
        // there is no reason for a second copy of them to exist.
        constexpr VkBufferUsageFlags sBuildInputUsage
            = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        constexpr VkBufferUsageFlags sStorageUsage
            = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        constexpr VkBufferUsageFlags sScratchUsage
            = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        /// Everything between a build and whatever reads the structure it wrote.
        void barrierAfterBuild(VkCommandBuffer commands)
        {
            const VkMemoryBarrier2 barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                .dstStageMask
                = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            };
            const VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &barrier,
            };
            vkCmdPipelineBarrier2(commands, &dependency);
        }
    }

    VkTransformMatrixKHR toVulkanTransform(const osg::Matrixf& matrix)
    {
        VkTransformMatrixKHR result{};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
                result.matrix[row][column] = matrix(column, row);

            result.matrix[row][3] = matrix(3, row);
        }
        return result;
    }

    SceneAcceleration::SceneAcceleration(const Device& device, CommandPool& pool, const SceneDesc& scene)
        : mDevice(device)
    {
        assert(!scene.getInstances().empty());

        mPositions = uploadBuffer(device, pool, scene.getPositions(), sBuildInputUsage);
        mIndices = uploadBuffer(device, pool, scene.getIndices(), sBuildInputUsage);
        device.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mPositions.getHandle()), "positions");
        device.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mIndices.getHandle()), "indices");

        buildBottomLevel(pool, scene);
        buildTopLevel(pool, scene);
    }

    SceneAcceleration::~SceneAcceleration()
    {
        const DeviceFunctions& functions = mDevice.getFunctions();

        if (mTopLevel != VK_NULL_HANDLE)
            functions.mDestroyAccelerationStructure(mDevice.getHandle(), mTopLevel, nullptr);

        for (const VkAccelerationStructureKHR structure : mBottomLevel)
            functions.mDestroyAccelerationStructure(mDevice.getHandle(), structure, nullptr);
    }

    void SceneAcceleration::buildBottomLevel(CommandPool& pool, const SceneDesc& scene)
    {
        const DeviceFunctions& functions = mDevice.getFunctions();
        const std::size_t count = scene.getMeshes().size();

        const VkDeviceAddress positions = mPositions.getDeviceAddress();
        const VkDeviceAddress indices = mIndices.getDeviceAddress();

        // The build reads these through pointers it keeps until the command is recorded, so they
        // live here rather than inside the loop.
        std::vector<VkAccelerationStructureGeometryKHR> geometries(count);
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(count);
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(count);
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(count);
        std::vector<VkDeviceSize> structureOffsets(count);
        std::vector<VkDeviceSize> scratchOffsets(count);
        std::vector<VkDeviceSize> structureSizes(count);

        const VkDeviceSize scratchAlignment
            = mDevice.getPhysicalDevice()
                  .getProperties()
                  .mAccelerationStructure.minAccelerationStructureScratchOffsetAlignment;

        VkDeviceSize structureTotal = 0;
        VkDeviceSize scratchTotal = 0;

        for (std::size_t i = 0; i < count; ++i)
        {
            const MeshRange& mesh = scene.getMeshes()[i];

            // Indices are mesh-local, so each structure is handed the slice of the shared buffers
            // that belongs to it and addresses vertex zero as its own first vertex.
            geometries[i] = VkAccelerationStructureGeometryKHR{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                .geometry = { .triangles = {
                                  .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                                  .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                                  .vertexData = { .deviceAddress
                                      = positions + VkDeviceSize{ mesh.mVertexOffset } * sizeof(osg::Vec3f) },
                                  .vertexStride = sizeof(osg::Vec3f),
                                  .maxVertex = mesh.mVertexCount - 1,
                                  .indexType = VK_INDEX_TYPE_UINT32,
                                  .indexData = { .deviceAddress
                                      = indices + VkDeviceSize{ mesh.mIndexOffset } * sizeof(std::uint32_t) },
                              } },
                // Opaque as built, and overridden per instance where a material says otherwise:
                // opacity is a property of the material and a mesh does not carry one, so the
                // top-level flags are the only place the question can be answered exactly.
                .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
            };

            builds[i] = VkAccelerationStructureBuildGeometryInfoKHR{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                // ALLOW_DATA_ACCESS is what lets a shader read a hit triangle's vertices back out of
                // the structure, which is the whole reason nothing here binds a vertex buffer.
                .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR,
                .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                .geometryCount = 1,
                .pGeometries = &geometries[i],
            };

            const std::uint32_t triangles = mesh.getTriangleCount();
            VkAccelerationStructureBuildSizesInfoKHR sizes{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            };
            functions.mGetAccelerationStructureBuildSizes(
                mDevice.getHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &builds[i], &triangles, &sizes);

            structureOffsets[i] = structureTotal;
            structureSizes[i] = sizes.accelerationStructureSize;
            structureTotal = alignUp(structureTotal + sizes.accelerationStructureSize, sStructureAlignment);

            scratchOffsets[i] = scratchTotal;
            scratchTotal = alignUp(scratchTotal + sizes.buildScratchSize, scratchAlignment);

            ranges[i] = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = triangles };
            rangePointers[i] = &ranges[i];
        }

        mStructureBytes = structureTotal;
        mBottomLevelStorage = Buffer(mDevice, structureTotal, sStorageUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        mDevice.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mBottomLevelStorage.getHandle()),
            "bottom level structures");

        // Scratch is transient: it is read and written by the build and never again. It goes out of
        // scope with this function, which is the only reason a cell's worth of it is affordable.
        const Buffer scratch(mDevice, scratchTotal, sScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const VkDeviceAddress scratchAddress = scratch.getDeviceAddress();

        mBottomLevel.resize(count, VK_NULL_HANDLE);
        for (std::size_t i = 0; i < count; ++i)
        {
            const VkAccelerationStructureCreateInfoKHR create{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                .buffer = mBottomLevelStorage.getHandle(),
                .offset = structureOffsets[i],
                .size = structureSizes[i],
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            };
            checkVk(functions.mCreateAccelerationStructure(mDevice.getHandle(), &create, nullptr, &mBottomLevel[i]),
                "vkCreateAccelerationStructureKHR");

            builds[i].dstAccelerationStructure = mBottomLevel[i];
            builds[i].scratchData.deviceAddress = scratchAddress + scratchOffsets[i];
        }

        pool.submitAndWait([&](VkCommandBuffer commands) {
            functions.mCmdBuildAccelerationStructures(
                commands, static_cast<std::uint32_t>(count), builds.data(), rangePointers.data());
            barrierAfterBuild(commands);
        });
    }

    void SceneAcceleration::buildTopLevel(CommandPool& pool, const SceneDesc& scene)
    {
        const DeviceFunctions& functions = mDevice.getFunctions();

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        instances.reserve(scene.getInstances().size());

        const std::span<const Material> materials = scene.getMaterials();

        for (const MeshInstance& instance : scene.getInstances())
        {
            const VkAccelerationStructureDeviceAddressInfoKHR address{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                .accelerationStructure = mBottomLevel[instance.mMesh],
            };

            const bool water
                = instance.mMaterial != sNoIndex && materials[instance.mMaterial].mKind == MaterialKind::Water;

            VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

            // This bit is what buys the candidate loop the chance to run: without it the geometry's
            // own opaque flag stands, traversal commits the first triangle it meets, and a canopy
            // stays the rectangle it was painted on.
            if (instance.mMaterial != sNoIndex && materials[instance.mMaterial].isCutout())
            {
                flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
                ++mCutoutInstanceCount;
            }

            instances.push_back(VkAccelerationStructureInstanceKHR{
                .transform = toVulkanTransform(instance.mTransform),
                .instanceCustomIndex = static_cast<std::uint32_t>(instances.size()) & 0xFFFFFFu,
                // Water is left out of what a shadow ray asks for. Sunlight reaching a seabed has
                // come through the surface, and a sea that occluded would black out every shallow in
                // the game — so it is told in the mask, where traversal skips it for nothing.
                .mask = static_cast<std::uint32_t>(water ? Shaders::MASK_WATER : Shaders::MASK_SOLID),
                .flags = flags,
                .accelerationStructureReference
                = functions.mGetAccelerationStructureDeviceAddress(mDevice.getHandle(), &address),
            });
        }

        mInstanceCount = static_cast<std::uint32_t>(instances.size());
        mInstances = uploadBuffer(
            mDevice, pool, std::span<const VkAccelerationStructureInstanceKHR>(instances), sBuildInputUsage);

        const VkAccelerationStructureGeometryKHR geometry{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = { .instances = {
                              .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                              .data = { .deviceAddress = mInstances.getDeviceAddress() },
                          } },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        };

        VkAccelerationStructureBuildGeometryInfoKHR build{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &geometry,
        };

        VkAccelerationStructureBuildSizesInfoKHR sizes{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        };
        functions.mGetAccelerationStructureBuildSizes(
            mDevice.getHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &mInstanceCount, &sizes);

        mStructureBytes += sizes.accelerationStructureSize;
        mTopLevelStorage
            = Buffer(mDevice, sizes.accelerationStructureSize, sStorageUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        const VkAccelerationStructureCreateInfoKHR create{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mTopLevelStorage.getHandle(),
            .size = sizes.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        };
        checkVk(functions.mCreateAccelerationStructure(mDevice.getHandle(), &create, nullptr, &mTopLevel),
            "vkCreateAccelerationStructureKHR");
        mDevice.setName(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, reinterpret_cast<std::uint64_t>(mTopLevel), "scene");
        mDevice.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mInstances.getHandle()), "instances");

        const Buffer scratch(mDevice, sizes.buildScratchSize, sScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        build.dstAccelerationStructure = mTopLevel;
        build.scratchData.deviceAddress = scratch.getDeviceAddress();

        const VkAccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = mInstanceCount };
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

        pool.submitAndWait([&](VkCommandBuffer commands) {
            functions.mCmdBuildAccelerationStructures(commands, 1, &build, &ranges);
            barrierAfterBuild(commands);
        });
    }
}
