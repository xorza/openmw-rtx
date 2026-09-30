#include "tracepipeline.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "pipeline.hpp"

namespace Rtx
{
    namespace
    {
        /// How many hit records `shaders` lays out: each closest-hit shader stands behind its run.
        std::size_t hitRecordsOf(const TraceShaders& shaders)
        {
            assert(shaders.mHitRecordsPerShader > 0 && "a closest-hit shader with no record to stand behind");
            return shaders.mHit.size() * shaders.mHitRecordsPerShader;
        }

        /// How many groups `shaders` makes: the ray generation stage's, one a miss, one a hit record.
        std::size_t groupsOf(const TraceShaders& shaders)
        {
            return 1 + shaders.mMiss.size() + hitRecordsOf(shaders);
        }
    }

    Owned<VkPipeline, vkDestroyPipeline> makeTracePipeline(const Device& device, const VkPipelineLayout layout,
        const TraceShaders& shaders, const std::string_view name, const std::span<const std::uint32_t> specialization)
    {
        PipelineCreation creation(device, name);
        const bool anyHitWanted = !shaders.mAnyHit.empty();

        // A stage is not a group: one any-hit is compiled and every hit group names it, and one
        // closest-hit stage stands behind a run of groups. The handles come back in group order,
        // which is the order `ShaderBindingTable` fills its records in.
        std::vector<ShaderModule> compiled;
        compiled.reserve(1 + shaders.mMiss.size() + (anyHitWanted ? 1 : 0) + shaders.mHit.size());

        const Specialization constants(specialization);

        // A closest-hit stage's own table, kept until the pipeline is made because the info the
        // stage names points into it. A deque, because `Specialization` points into itself and may
        // not move.
        std::deque<Specialization> hitConstants;

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        stages.reserve(compiled.capacity());
        groups.reserve(groupsOf(shaders));

        const auto addStage
            = [&](VkShaderStageFlagBits stage, const std::string_view module, const VkSpecializationInfo* specialized) {
                  const auto at = static_cast<std::uint32_t>(stages.size());
                  compiled.push_back(loadShaderModule(device, module));
                  stages.push_back(VkPipelineShaderStageCreateInfo{
                      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                      .stage = stage,
                      .module = compiled.back().get(),
                      .pName = "main",
                      .pSpecializationInfo = specialized,
                  });

                  return at;
              };

        const auto addGeneral = [&](std::uint32_t stage) {
            groups.push_back(VkRayTracingShaderGroupCreateInfoKHR{
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                .generalShader = stage,
                .closestHitShader = VK_SHADER_UNUSED_KHR,
                .anyHitShader = VK_SHADER_UNUSED_KHR,
                .intersectionShader = VK_SHADER_UNUSED_KHR,
            });
        };

        addGeneral(addStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, shaders.mRaygen, constants.getInfo()));
        for (const std::string_view module : shaders.mMiss)
            addGeneral(addStage(VK_SHADER_STAGE_MISS_BIT_KHR, module, constants.getInfo()));

        const std::uint32_t anyHit = anyHitWanted
            ? addStage(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, shaders.mAnyHit, constants.getInfo())
            : VK_SHADER_UNUSED_KHR;

        for (const HitShader& hit : shaders.mHit)
        {
            const VkSpecializationInfo* specialized = hit.mSpecialization.empty()
                ? constants.getInfo()
                : hitConstants.emplace_back(hit.mSpecialization).getInfo();

            const std::uint32_t closestHit = addStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, hit.mModule, specialized);
            for (std::uint32_t record = 0; record < shaders.mHitRecordsPerShader; ++record)
                groups.push_back(VkRayTracingShaderGroupCreateInfoKHR{
                    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                    .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
                    .generalShader = VK_SHADER_UNUSED_KHR,
                    .closestHitShader = closestHit,
                    .anyHitShader = anyHit,
                    .intersectionShader = VK_SHADER_UNUSED_KHR,
                });
        }

        // What the pipeline promises the driver it will never do, so the driver may leave those
        // paths out of traversal: no procedural geometry anywhere in this renderer, so no AABB is
        // ever traversed — which is also why no promise about intersection shaders is made, since
        // a launch of ray queries alone binds no hit table for the layers to check one against;
        // and where a stage kind is present at all, every group names one, so no null shader of
        // that kind is ever called. NVIDIA's best-practice list asks for each of these "whenever
        // possible".
        VkPipelineCreateFlags promises = VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR;
        if (!shaders.mMiss.empty())
            promises |= VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR;
        if (!shaders.mHit.empty())
            promises |= VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR;
        if (anyHitWanted)
            promises |= VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR;

        const VkRayTracingPipelineCreateInfoKHR pipeline{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            .pNext = creation.chain(nullptr),
            // The statistics are asked for and, on this driver, not answered: NVIDIA reports one
            // executable for every compute pipeline in this renderer and none at all for a ray
            // tracing one — `Device::reportPipeline` is where that shows. Asked for anyway,
            // because it costs the frame nothing and is what makes the report appear the day a
            // driver answers.
            .flags = PipelineCreation::sFlags | promises,
            .stageCount = static_cast<std::uint32_t>(stages.size()),
            .pStages = stages.data(),
            .groupCount = static_cast<std::uint32_t>(groups.size()),
            .pGroups = groups.data(),
            // One, and one is the whole of it. Ray generation calls `traceRayEXT` and the shader
            // the hit selects runs; that shader traces again with inline ray queries, which are
            // not recursion and cost the stack nothing. Nothing a hit or a miss runs calls
            // `traceRayEXT`, so no second level exists to be sized for.
            .maxPipelineRayRecursionDepth = 1,
            .layout = layout,
        };
        Owned<VkPipeline, vkDestroyPipeline> handle;
        checkVk(device.getFunctions().mCreateRayTracingPipelines(device.getHandle(), VK_NULL_HANDLE,
                    device.getPipelineCache(), 1, &pipeline, nullptr, handle.put(device)),
            "vkCreateRayTracingPipelinesKHR");

        creation.finish(handle.get());
        return handle;
    }

    ShaderBindingTable::ShaderBindingTable(
        const Device& device, const VkPipeline pipeline, const TraceShaders& shaders, const std::string_view name)
        : mDevice(device)
    {
        const std::size_t hitRecords = hitRecordsOf(shaders);
        assert((hitRecords == 0 ? shaders.mHitRecordData.empty() : shaders.mHitRecordData.size() % hitRecords == 0)
            && "hit record data that does not divide into one block per record");
        const std::size_t hitRecordBytes = hitRecords == 0 ? 0 : shaders.mHitRecordData.size() / hitRecords;
        const std::size_t groups = groupsOf(shaders);

        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& limits
            = device.getPhysicalDevice().getProperties().mRayTracingPipeline;

        // One record per group, in the order the groups were appended, packed at the handle's own
        // alignment. What separates the three regions is the coarser base alignment below.
        const VkDeviceSize stride = alignUp(limits.shaderGroupHandleSize, limits.shaderGroupHandleAlignment);

        // A hit record is its handle and then its data, and the region's stride is what the data
        // adds, rounded back up to the handle's alignment.
        const VkDeviceSize hitStride
            = alignUp(limits.shaderGroupHandleSize + hitRecordBytes, limits.shaderGroupHandleAlignment);

        // A region's size is its stride for the ray generation stage, and both are aligned to
        // the base alignment rather than the handle's, which is what makes that one record longer
        // than the two kinds beside it.
        const VkDeviceSize raygenStride = alignUp(stride, limits.shaderGroupBaseAlignment);

        const auto region = [&](VkDeviceSize& at, VkDeviceSize each, std::size_t count) {
            at = alignUp(at, limits.shaderGroupBaseAlignment);
            const VkStridedDeviceAddressRegionKHR described{
                .deviceAddress = at,
                .stride = count > 0 ? each : 0,
                .size = each * count,
            };
            at += described.size;

            return described;
        };

        VkDeviceSize at = 0;
        mRaygen = region(at, raygenStride, 1);
        mMiss = region(at, stride, shaders.mMiss.size());
        mHit = region(at, hitStride, hitRecords);

        // Filled as unnamed, because this runs on a compile thread while the main thread submits,
        // and nothing has named the table yet: `traceRays` is the first hand-out.
        mTable = Buffer::hostWritten(
            device, at, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, name);
        const std::span<std::byte> table = mTable.unnamed<std::byte>();
        std::memset(table.data(), 0, table.size());

        std::vector<std::uint8_t> handles(groups * limits.shaderGroupHandleSize);
        checkVk(device.getFunctions().mGetRayTracingShaderGroupHandles(device.getHandle(), pipeline, 0,
                    static_cast<std::uint32_t>(groups), handles.size(), handles.data()),
            "vkGetRayTracingShaderGroupHandlesKHR");

        // Each region's records, in the order their groups were made: the handles came back packed
        // at the handle size and go out at the stride the region was laid out with, a hit record's
        // data right after its handle. The regions still hold offsets here, which is what a write
        // into the table wants.
        std::uint32_t group = 0;
        const auto fill = [&](const VkStridedDeviceAddressRegionKHR& into, std::size_t count, std::size_t bytes) {
            for (std::size_t record = 0; record < count; ++record, ++group)
            {
                const VkDeviceSize address = into.deviceAddress + record * into.stride;
                std::memcpy(table.data() + address, handles.data() + group * limits.shaderGroupHandleSize,
                    limits.shaderGroupHandleSize);
                if (bytes > 0)
                    std::memcpy(table.data() + address + limits.shaderGroupHandleSize,
                        shaders.mHitRecordData.data() + record * bytes, bytes);
            }
        };
        fill(mRaygen, 1, 0);
        fill(mMiss, shaders.mMiss.size(), 0);
        fill(mHit, hitRecords, hitRecordBytes);

        // A shader binding table's buffer is placed at the base alignment by the allocator —
        // `alignmentOwedBy` asks for it — so its address is aligned without offsetting into it.
        // Asserted rather than worked around, because a table that has to be offset into is a
        // table this renderer does not have.
        const VkDeviceAddress base = mTable.getDeviceAddress();
        assert(base % limits.shaderGroupBaseAlignment == 0
            && "a shader binding table the device would not read from where it was put");

        // The regions were laid out as offsets and become addresses once there is a buffer under
        // them. An empty region keeps its nought, which is what an unused one is.
        for (VkStridedDeviceAddressRegionKHR* described : { &mRaygen, &mMiss, &mHit })
            described->deviceAddress = described->size > 0 ? base + described->deviceAddress : 0;
    }

    void ShaderBindingTable::traceRays(
        VkCommandBuffer commands, std::uint32_t width, std::uint32_t height, std::uint32_t depth) const
    {
        // The regions carry the table by address, taken once at the fill, so the launch is the
        // hand-out that names it: the table then outlives every trace that reads it.
        mTable.nameForNext();
        mDevice.getFunctions().mCmdTraceRays(commands, &mRaygen, &mMiss, &mHit, &mCallable, width, height, depth);
    }
}
