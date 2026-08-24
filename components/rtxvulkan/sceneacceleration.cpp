#include "sceneacceleration.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include <components/rtx/error.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/shaders/scene.h>

#include "commands.hpp"
#include "device.hpp"
#include "gputimer.hpp"
#include "result.hpp"

namespace Rtx
{
    namespace
    {
        /// Brackets a build where there is a timer to bracket it with.
        ///
        /// **The load path has none.** Building every structure from scratch is a cell arriving and
        /// not a frame, and its cost is already reported as a build time; giving it zones would put
        /// them in whichever frame report came next.
        void openZone(GpuTimer* timer, VkCommandBuffer commands, std::string_view name)
        {
            if (timer != nullptr)
                timer->open(commands, name);
        }

        void closeZone(GpuTimer* timer, VkCommandBuffer commands)
        {
            if (timer != nullptr)
                timer->close(commands);
        }

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

    VkTransformMatrixKHR toVulkanTransform(const Transform3x4& transform)
    {
        VkTransformMatrixKHR result{};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                result.matrix[row][column] = transform.mRows[row][column];

        return result;
    }

    SceneAcceleration::SceneAcceleration(
        const Device& device, Batch& batch, const SceneDesc& scene, std::span<const InstanceRecord> records)
        : mDevice(device)
    {
        assert(scene.getPlacedCount() > 0);

        uploadGeometry(batch, scene);

        // **The indices, every bottom level and the top level in one submit.** Each was its own
        // round trip and each is ordered against the last by a barrier the recording already
        // carries: `uploadBuffer` ends in one, and `buildBottomLevel` ends in `barrierAfterBuild`.
        buildBottomLevel(batch, scene);
        prepareTopLevel(scene, records);
        recordTopLevel(batch.getCommands(), nullptr);
    }

    SceneAcceleration::~SceneAcceleration()
    {
        const DeviceFunctions& functions = mDevice.getFunctions();

        if (mTopLevel != VK_NULL_HANDLE)
            functions.mDestroyAccelerationStructure(mDevice.getHandle(), mTopLevel, nullptr);

        for (const VkAccelerationStructureKHR structure : mBottomLevel)
            if (structure != VK_NULL_HANDLE)
                functions.mDestroyAccelerationStructure(mDevice.getHandle(), structure, nullptr);
    }

    void SceneAcceleration::uploadGeometry(Batch& batch, const SceneDesc& scene)
    {
        const std::span<const osg::Vec3f> positions = scene.getPositions();
        for (std::uint32_t block = 0; block < mPositions.blocksFor(static_cast<std::uint32_t>(positions.size()));
             ++block)
        {
            const std::size_t from = std::size_t{ block } * mPositions.getBlockSize();
            const std::size_t count = std::min<std::size_t>(mPositions.getBlockSize(), positions.size() - from);

            HostBuffer made(mDevice, mPositions.getBlockBytes(), sBuildInputUsage);
            made.fillFrom(positions.subspan(from, count));
            mDevice.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(made.getHandle()),
                "positions " + std::to_string(block));
            mPositions.add(std::move(made));
        }

        const std::span<const std::uint32_t> indices = scene.getIndices();
        for (std::uint32_t block = 0; block < mIndices.blocksFor(static_cast<std::uint32_t>(indices.size())); ++block)
        {
            const std::size_t from = std::size_t{ block } * mIndices.getBlockSize();
            const std::size_t count = std::min<std::size_t>(mIndices.getBlockSize(), indices.size() - from);

            Buffer made = uploadBuffer(mDevice, batch, std::as_bytes(indices.subspan(from, count)), sBuildInputUsage,
                mIndices.getBlockBytes());
            mDevice.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(made.getHandle()),
                "indices " + std::to_string(block));
            mIndices.add(std::move(made));
        }

        const std::span<const VkDeviceAddress> table = mIndices.getAddresses();
        mIndexBlocks = HostBuffer(mDevice, table.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        mIndexBlocks.write(table);
        mDevice.setName(
            VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mIndexBlocks.getHandle()), "index blocks");
    }

    void SceneAcceleration::buildBottomLevel(Batch& batch, const SceneDesc& scene)
    {
        const DeviceFunctions& functions = mDevice.getFunctions();
        const std::size_t count = scene.getMeshes().size();

        // The build reads these through pointers it keeps until the command is recorded, so they
        // live here rather than inside the loop.
        std::vector<VkAccelerationStructureGeometryKHR> geometries(count);
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(count);
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(count);
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(count);
        std::vector<VkDeviceSize> structureOffsets(count);
        std::vector<VkDeviceSize> scratchOffsets(count);
        std::vector<VkDeviceSize> structureSizes(count);
        mBuildScratch.resize(count);

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
                                  .vertexData = { .deviceAddress = mPositions.addressOf(mesh.mVertexOffset) },
                                  .vertexStride = sizeof(osg::Vec3f),
                                  // **Guarded, because a freed slot has no vertices.** A slot the
                                  // scene has taken back keeps its index and its room and holds a
                                  // count of zero until something fits into it; subtracting one
                                  // there wraps, and the driver is handed four billion vertices.
                                  .maxVertex = mesh.mVertexCount > 0 ? mesh.mVertexCount - 1 : 0,
                                  .indexType = VK_INDEX_TYPE_UINT32,
                                  .indexData = { .deviceAddress = mIndices.addressOf(mesh.mIndexOffset) },
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

            // **A freed slot gets no structure at all.** It keeps its index and its room and holds
            // nothing until something fits into it, and a build over no primitives is not a small
            // structure — it is a size the driver may answer zero for, which is not a size an
            // acceleration structure can be created at.
            if (triangles == 0)
            {
                structureSizes[i] = 0;
                ranges[i] = VkAccelerationStructureBuildRangeInfoKHR{};
                rangePointers[i] = &ranges[i];
                mBuildScratch[i] = 0;
                continue;
            }

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

            // Kept so a rebuild of this one mesh does not have to ask the driver its size again.
            // The same geometry describes it, so the answer cannot have changed.
            mBuildScratch[i] = sizes.buildScratchSize;

            ranges[i] = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = triangles };
            rangePointers[i] = &ranges[i];
        }

        mBottomLevelBytes = structureTotal;
        mBottomLevelStorage = Buffer(mDevice, structureTotal, sStorageUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        mDevice.setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(mBottomLevelStorage.getHandle()),
            "bottom level structures");

        // Scratch is transient: it is read and written by the build and never again. It is handed to
        // the batch below rather than left to this scope, because the build it feeds has only been
        // recorded when this function returns — and the batch frees it the moment the flush does.
        Buffer scratch(mDevice, scratchTotal, sScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const VkDeviceAddress scratchAddress = scratch.getDeviceAddress();

        // Which slots actually have a structure, so the build below is handed those and only those.
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> live;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> liveRanges;
        live.reserve(count);
        liveRanges.reserve(count);

        mBottomLevel.assign(count, VK_NULL_HANDLE);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (structureSizes[i] == 0)
                continue;

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

            live.push_back(builds[i]);
            liveRanges.push_back(rangePointers[i]);
        }

        // **Asked once each, here, and never again.** These handles last until the next `setScene`
        // and their addresses with them, so the alternative is the same question per instance per
        // frame — fifty thousand driver round trips on a nine-by-nine exterior for fifty thousand
        // answers that cannot have changed.
        // Zero for a slot with no structure, which nothing asks for: a placement only ever names a
        // mesh that has one.
        mBottomLevelAddresses.assign(count, 0);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (mBottomLevel[i] == VK_NULL_HANDLE)
                continue;

            const VkAccelerationStructureDeviceAddressInfoKHR address{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                .accelerationStructure = mBottomLevel[i],
            };
            mBottomLevelAddresses[i] = functions.mGetAccelerationStructureDeviceAddress(mDevice.getHandle(), &address);
        }

        if (live.empty())
            return;

        const VkCommandBuffer commands = batch.getCommands();
        functions.mCmdBuildAccelerationStructures(
            commands, static_cast<std::uint32_t>(live.size()), live.data(), liveRanges.data());
        barrierAfterBuild(commands);

        batch.keep(std::move(scratch));
    }

    void SceneAcceleration::prepareRefit(const SceneDesc& scene)
    {
        const std::span<const Index> deformed = scene.getDeformed();
        if (deformed.empty())
        {
            // **Emptied and not left alone.** These still hold the last frame's rebuilds, and a
            // frame whose actors have all gone would otherwise leave a vector whose size claims work
            // that is not there.
            mRefitBuilds.clear();
            return;
        }

        const auto count = static_cast<std::uint32_t>(deformed.size());

        const VkDeviceSize scratchAlignment
            = mDevice.getPhysicalDevice()
                  .getProperties()
                  .mAccelerationStructure.minAccelerationStructureScratchOffsetAlignment;

        VkDeviceSize scratchTotal = 0;
        for (const Index mesh : deformed)
        {
            assert(mesh < mBottomLevel.size() && "a mesh this holds no structure for");
            scratchTotal = alignUp(scratchTotal + mBuildScratch[mesh], scratchAlignment);
        }

        if (mRefitScratch.getSize() < scratchTotal)
            mRefitScratch = Buffer(mDevice, scratchTotal, sScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        const VkDeviceAddress scratchAddress = mRefitScratch.getDeviceAddress();

        mRefitGeometries.resize(count);
        mRefitBuilds.resize(count);
        mRefitRanges.resize(count);
        mRefitRangePointers.resize(count);

        for (std::uint32_t i = 0; i < count; ++i)
        {
            const Index index = deformed[i];
            const MeshRange& mesh = scene.getMeshes()[index];

            // **Straight into the memory the builder reads**, with no staging buffer between and no
            // copy to record. The submit below carries an implicit dependency on host writes made
            // before it, which is what a barrier would otherwise have been for.
            mPositions.at(mesh.mVertexOffset)
                .writeAt(mPositions.offsetOf(mesh.mVertexOffset), scene.getMeshPositions(index));

            // The same description the first build was given, which is what makes the structure it
            // produces the same size as the one already sitting at this mesh's offset.
            mRefitGeometries[i] = VkAccelerationStructureGeometryKHR{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                .geometry = { .triangles = {
                                  .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                                  .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                                  .vertexData = { .deviceAddress = mPositions.addressOf(mesh.mVertexOffset) },
                                  .vertexStride = sizeof(osg::Vec3f),
                                  // **Guarded, because a freed slot has no vertices.** A slot the
                                  // scene has taken back keeps its index and its room and holds a
                                  // count of zero until something fits into it; subtracting one
                                  // there wraps, and the driver is handed four billion vertices.
                                  .maxVertex = mesh.mVertexCount > 0 ? mesh.mVertexCount - 1 : 0,
                                  .indexType = VK_INDEX_TYPE_UINT32,
                                  .indexData = { .deviceAddress = mIndices.addressOf(mesh.mIndexOffset) },
                              } },
                .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
            };

            mRefitRanges[i] = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = mesh.getTriangleCount() };
            mRefitRangePointers[i] = &mRefitRanges[i];
        }

        // A second pass, because `pGeometries` is a pointer into a vector the first pass was still
        // filling: a build info written beside a geometry that later moved would name freed memory.
        VkDeviceSize scratchAt = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const Index index = deformed[i];

            // **Built into the structure that is already there**, rather than into a new one beside
            // it. A build overwrites its destination outright, the geometry it is given is the same
            // shape as last time so it needs no more room, and the structure's handle is what every
            // top-level instance already points at.
            mRefitBuilds[i] = VkAccelerationStructureBuildGeometryInfoKHR{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR,
                .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                .dstAccelerationStructure = mBottomLevel[index],
                .geometryCount = 1,
                .pGeometries = &mRefitGeometries[i],
                .scratchData = { .deviceAddress = scratchAddress + scratchAt },
            };

            scratchAt = alignUp(scratchAt + mBuildScratch[index], scratchAlignment);
        }
    }

    void SceneAcceleration::recordRefit(VkCommandBuffer commands, GpuTimer* timer)
    {
        openZone(timer, commands, "refit");
        mDevice.getFunctions().mCmdBuildAccelerationStructures(
            commands, static_cast<std::uint32_t>(mRefitBuilds.size()), mRefitBuilds.data(), mRefitRangePointers.data());
        barrierAfterBuild(commands);
        closeZone(timer, commands);
    }

    void SceneAcceleration::place(
        CommandPool& pool, const SceneDesc& scene, std::span<const InstanceRecord> records, GpuTimer* timer)
    {
        prepareRefit(scene);
        prepareTopLevel(scene, records);

        // **One submit, and the barrier between them is what the fence used to be.** The top level
        // is built over structures the refit has just rewritten, which is a dependency inside a
        // command buffer rather than a reason to go round the driver twice.
        pool.submitAndWait([&](VkCommandBuffer commands) {
            if (!mRefitBuilds.empty())
                recordRefit(commands, timer);

            recordTopLevel(commands, timer);
        });
    }

    void SceneAcceleration::prepareTopLevel(const SceneDesc& scene, std::span<const InstanceRecord> records)
    {
        const DeviceFunctions& functions = mDevice.getFunctions();

        // **Checked here rather than left to the driver.** A scene that grew a mesh since `setScene`
        // built the structures is a caller breaking `placeScene`'s contract, and the only symptom is
        // a top level naming a bottom level that was never made — which surfaces as an invalid handle
        // inside `vkGetAccelerationStructureDeviceAddressKHR` and says nothing about who did it. One
        // comparison, once a frame, for a failure that otherwise takes the process down unexplained.
        if (scene.getMeshes().size() != mBottomLevel.size())
            throw Error("the scene grew from " + std::to_string(mBottomLevel.size()) + " meshes to "
                + std::to_string(scene.getMeshes().size())
                + " without being built again; placeScene can only move what setScene made");

        mCutoutInstanceCount = 0;
        mRowScratch.clear();
        mRowScratch.reserve(records.size());

        for (std::uint32_t index = 0; index < records.size(); ++index)
        {
            const InstanceRecord& record = records[index];

            // **A gap contributes no row rather than a masked one.** Its slot still names it — the
            // custom index below is the slot and not the row — so skipping costs the build one
            // primitive it would otherwise have to sort and never hit.
            if (!record.mPlaced)
                continue;

            // Morrowind's sheet geometry is lit and hit from both faces, so nothing is culled.
            VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            if (record.mCutout)
            {
                flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

                // Counted here rather than in a pass of its own: this loop already visits every
                // record and skips the same gaps, and a scene is tens of thousands of them.
                ++mCutoutInstanceCount;
            }

            mRowScratch.push_back(VkAccelerationStructureInstanceKHR{
                .transform = toVulkanTransform(record.mTransform),
                // A record's position is the custom index the shader reads back at a hit.
                .instanceCustomIndex = index & 0xFFFFFFu,
                .mask = record.mMask,
                .flags = flags,
                .accelerationStructureReference = mBottomLevelAddresses[record.mMesh],
            });
        }

        mInstanceCount = static_cast<std::uint32_t>(mRowScratch.size());

        // **Written where the builder reads it.** These are rewritten whole every frame, so staging
        // them was a buffer made, a copy recorded, a submit and a wait on the queue for a memcpy.
        const std::span<const VkAccelerationStructureInstanceKHR> rows(mRowScratch);
        if (mInstances.getSize() < rows.size_bytes())
            mInstances = HostBuffer(mDevice, rows.size_bytes(), sBuildInputUsage);

        mInstances.write(rows);

        mTopLevelGeometry = VkAccelerationStructureGeometryKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = { .instances = {
                              .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                              .data = { .deviceAddress = mInstances.getDeviceAddress() },
                          } },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        };

        mTopLevelBuild = VkAccelerationStructureBuildGeometryInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &mTopLevelGeometry,
        };

        VkAccelerationStructureBuildSizesInfoKHR sizes{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        };
        functions.mGetAccelerationStructureBuildSizes(mDevice.getHandle(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &mTopLevelBuild, &mInstanceCount, &sizes);

        // **The old structure goes before the new one is made over the same buffer.** Nothing is
        // reading it: every submit on this path waits, and so does the trace, so by the time a frame
        // is placing the world again the last one has finished with it.
        if (mTopLevel != VK_NULL_HANDLE)
        {
            functions.mDestroyAccelerationStructure(mDevice.getHandle(), mTopLevel, nullptr);
            mTopLevel = VK_NULL_HANDLE;
        }

        mTopLevelBytes = sizes.accelerationStructureSize;

        // Grown to the high-water mark and kept, both of them. A structure is created at offset zero
        // of whatever this holds and asks only that it be large enough.
        if (mTopLevelStorage.getSize() < sizes.accelerationStructureSize)
            mTopLevelStorage
                = Buffer(mDevice, sizes.accelerationStructureSize, sStorageUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (mTopLevelScratch.getSize() < sizes.buildScratchSize)
            mTopLevelScratch
                = Buffer(mDevice, sizes.buildScratchSize, sScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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

        mTopLevelBuild.dstAccelerationStructure = mTopLevel;
        mTopLevelBuild.scratchData.deviceAddress = mTopLevelScratch.getDeviceAddress();
    }

    void SceneAcceleration::recordTopLevel(VkCommandBuffer commands, GpuTimer* timer)
    {
        const VkAccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = mInstanceCount };
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

        openZone(timer, commands, "tlas");
        mDevice.getFunctions().mCmdBuildAccelerationStructures(commands, 1, &mTopLevelBuild, &ranges);
        barrierAfterBuild(commands);
        closeZone(timer, commands);
    }
}
