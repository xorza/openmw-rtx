#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/sets.h>
#include <components/rtxvulkan/device/bindingtable.hpp>
#include <components/rtxvulkan/device/handles.hpp>

#include "computepipeline.hpp"
#include "pipeline.hpp"

namespace Rtx
{
    /// How many workgroups of `workgroup` lanes cover `extent` of them. One statement for every
    /// pass, because a dispatch that fell a group short would leave a stripe of the frame
    /// untouched — and a rounding written once is one a test can hold.
    constexpr std::uint32_t groupsFor(std::uint32_t extent, std::uint32_t workgroup)
    {
        return (extent + workgroup - 1) / workgroup;
    }

    /// How many workgroups a dispatch launches along each axis, made from the extent they cover
    /// and the workgroup's edges wherever they cover one, so a pass never pairs a width with
    /// another pass's workgroup.
    struct Groups
    {
        std::uint32_t mX = 1;
        std::uint32_t mY = 1;
        std::uint32_t mZ = 1;

        /// The groups of `workgroup` lanes that cover `lanes` in a row.
        static constexpr Groups along(std::uint32_t lanes, std::uint32_t workgroup)
        {
            return Groups{ .mX = groupsFor(lanes, workgroup) };
        }

        /// The groups of `across` by `down` lanes that cover `width` by `height`.
        static constexpr Groups covering(
            std::uint32_t width, std::uint32_t height, std::uint32_t across, std::uint32_t down)
        {
            return Groups{ .mX = groupsFor(width, across), .mY = groupsFor(height, down) };
        }

        /// The groups of a square `workgroup` lanes on a side that cover `width` by `height`.
        static constexpr Groups covering(std::uint32_t width, std::uint32_t height, std::uint32_t workgroup)
        {
            return covering(width, height, workgroup, workgroup);
        }
    };

    /// One binding of set zero, visible to the compute stage.
    constexpr VkDescriptorSetLayoutBinding computeBinding(std::uint32_t slot, VkDescriptorType type)
    {
        return VkDescriptorSetLayoutBinding{ slot, type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    }

    /// `Count` bindings of one type, numbered from zero. A pass that mixes descriptor types builds
    /// its own list out of `computeBinding`.
    template <std::size_t Count>
    constexpr std::array<VkDescriptorSetLayoutBinding, Count> computeBindings(VkDescriptorType type)
    {
        std::array<VkDescriptorSetLayoutBinding, Count> bindings{};
        for (std::uint32_t slot = 0; slot < Count; ++slot)
            bindings[slot] = computeBinding(slot, type);

        return bindings;
    }

    /// The writes one push or one set update is made of, and the infos they point at, in one
    /// object: a write names its info by address, so the two live together and neither moves —
    /// which is why this is neither copied nor moved, and why it is a local of the pass that
    /// fills it.
    ///
    /// **Made against the table of the set it writes**, which is the one statement of each
    /// binding's type and count: a write names a binding and hands a descriptor, and the type is
    /// the table's. Appended in binding order, every binding of the table once, which a debug
    /// build asserts — so a binding a shader grew and a pass did not write, and two writes the pass
    /// swapped, are failures and not a dispatch reading whatever the slot held.
    class DescriptorWrites
    {
    public:
        /// The most image infos one push or update carries: the fog volume's twenty images.
        static constexpr std::size_t sMostImages = BindingTable::sMost;

        /// For a push against `pipeline`'s own set.
        explicit DescriptorWrites(const Pipeline& pipeline)
            : mTable(pipeline.getBindings())
        {
        }

        /// For an update of `set`, a set of `layout`, through `vkUpdateDescriptorSets`.
        DescriptorWrites(const SetLayout& layout, VkDescriptorSet set)
            : mTable(layout.getBindings())
            , mSet(set)
        {
        }

        DescriptorWrites(const DescriptorWrites&) = delete;
        DescriptorWrites& operator=(const DescriptorWrites&) = delete;

        void image(std::uint32_t binding, const VkDescriptorImageInfo& info)
        {
            images(binding, std::span<const VkDescriptorImageInfo>(&info, 1));
        }

        /// One binding that is an array of as many images as the table gives it.
        void images(std::uint32_t binding, std::span<const VkDescriptorImageInfo> infos)
        {
            const VkDescriptorSetLayoutBinding& entry = declared(binding, infos.size());
            assert((entry.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                       || entry.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                       || entry.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                       || entry.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)
                && "an image written to a binding the table declares as something else");
            assert(mImageCount + infos.size() <= mImages.size() && "more image infos than there is room for");

            const VkDescriptorImageInfo* const at = mImages.data() + mImageCount;
            for (const VkDescriptorImageInfo& info : infos)
                mImages[mImageCount++] = info;

            append(entry, nullptr, at, nullptr);
        }

        void buffer(std::uint32_t binding, const VkDescriptorBufferInfo& info)
        {
            const VkDescriptorSetLayoutBinding& entry = declared(binding, 1);
            assert((entry.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                       || entry.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                && "a buffer written to a binding the table declares as something else");

            mBuffers[mBufferCount] = info;
            append(entry, nullptr, nullptr, &mBuffers[mBufferCount++]);
        }

        /// The one write whose payload hangs off `pNext` rather than off a pointer field. `next`
        /// has to outlive the push, as the structure it names does.
        void structure(std::uint32_t binding, const VkWriteDescriptorSetAccelerationStructureKHR& next)
        {
            const VkDescriptorSetLayoutBinding& entry = declared(binding, 1);
            assert(entry.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
                && "a structure written to a binding the table declares as something else");
            append(entry, &next, nullptr, nullptr);
        }

        /// Every write, once every binding of the table has one.
        std::span<const VkWriteDescriptorSet> get() const
        {
            assert(mCount == mTable.get().size() && "a binding the table declares was left unwritten");
            return std::span<const VkWriteDescriptorSet>(mWrites.data(), mCount);
        }

        /// Whether these were made against `table`: a push is only defined against the layout in
        /// force, however alike two layouts are.
        bool against(const BindingTable& table) const { return &mTable == &table; }

    private:
        /// The table's entry for the next write, which has to be `binding`, and hold `count`.
        const VkDescriptorSetLayoutBinding& declared(
            [[maybe_unused]] std::uint32_t binding, [[maybe_unused]] std::size_t count) const
        {
            assert(mCount < mTable.get().size() && "more descriptor writes than the table has bindings");
            const VkDescriptorSetLayoutBinding& next = mTable.get()[mCount];
            assert(next.binding == binding && "descriptor writes out of the table's binding order");
            assert(next.descriptorCount == count && "a binding written with another count than the table's");
            return next;
        }

        void append(const VkDescriptorSetLayoutBinding& entry, const void* next, const VkDescriptorImageInfo* image,
            const VkDescriptorBufferInfo* block)
        {
            mWrites[mCount++] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = next,
                .dstSet = mSet,
                .dstBinding = entry.binding,
                .dstArrayElement = 0,
                .descriptorCount = entry.descriptorCount,
                .descriptorType = entry.descriptorType,
                .pImageInfo = image,
                .pBufferInfo = block,
                .pTexelBufferView = nullptr,
            };
        }

        const BindingTable& mTable;
        VkDescriptorSet mSet = VK_NULL_HANDLE;
        std::array<VkDescriptorImageInfo, sMostImages> mImages{};
        std::array<VkDescriptorBufferInfo, BindingTable::sMost> mBuffers{};
        std::array<VkWriteDescriptorSet, BindingTable::sMost> mWrites{};
        std::size_t mImageCount = 0;
        std::size_t mBufferCount = 0;
        std::size_t mCount = 0;
    };

    /// Pushes `SET_PASS`, which every pipeline here declares as a push descriptor set, from writes
    /// made against that pipeline's own table.
    inline void pushDescriptors(VkCommandBuffer commands, const Pipeline& pipeline, const DescriptorWrites& writes)
    {
        assert(writes.against(pipeline.getBindings()) && "writes made against another layout than the one pushed to");

        const std::span<const VkWriteDescriptorSet> pushed = writes.get();
        vkCmdPushDescriptorSet(commands, pipeline.getBindPoint(), pipeline.getLayout(), Shaders::SET_PASS,
            static_cast<std::uint32_t>(pushed.size()), pushed.data());
    }

    /// Binds, pushes and launches: the calls every compute pass in this backend ends with. A
    /// pipeline pushed nothing is handed `NoConstants{}` and pushes no block.
    template <class Constants>
    void dispatch(VkCommandBuffer commands, const ComputePipeline<Constants>& pipeline, const DescriptorWrites& writes,
        const std::type_identity_t<Constants>& constants, const Groups groups)
    {
        bind(commands, pipeline);
        pushDescriptors(commands, pipeline, writes);
        if constexpr (!std::is_same_v<Constants, NoConstants>)
            pipeline.push(commands, constants);
        vkCmdDispatch(commands, groups.mX, groups.mY, groups.mZ);
    }

    /// The same for a pipeline that binds nothing of its own, because a push of nought writes is
    /// not a push Vulkan takes.
    template <class Constants>
    void dispatch(VkCommandBuffer commands, const ComputePipeline<Constants>& pipeline,
        const std::type_identity_t<Constants>& constants, const Groups groups)
    {
        assert(pipeline.getBindings().get().empty() && "a dispatch that writes none of the bindings it declares");

        bind(commands, pipeline);
        pipeline.push(commands, constants);
        vkCmdDispatch(commands, groups.mX, groups.mY, groups.mZ);
    }
}
