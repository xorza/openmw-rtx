#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/sets.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/owned.hpp>

namespace Rtx
{
    /// The map entries a table of specialization words needs, and the `VkSpecializationInfo` over
    /// them. Built here rather than by the caller, because it is the same table every time and
    /// its contents are the caller's own indices: `constant_id` `i` takes word `i`, at word `i`'s
    /// offset. Words because that is what every constant this renderer specializes on is — a `bool`
    /// reaches SPIR-V as a 32-bit value like a `uint` does. The words are copied, so nothing
    /// outlives the object but what `getInfo` points at, which is the object's own.
    class Specialization
    {
    public:
        /// @param words the pipeline's, one per constant from nought.
        /// @param more a stage's own after them, for a module compiled into several stages under
        ///        constants of its own — `TraceShaders::mHit`.
        explicit Specialization(std::span<const std::uint32_t> words, std::span<const std::uint32_t> more = {})
            : mWords(words.begin(), words.end())
        {
            mWords.insert(mWords.end(), more.begin(), more.end());

            mEntries.resize(mWords.size());
            for (std::uint32_t at = 0; at < mEntries.size(); ++at)
                mEntries[at] = VkSpecializationMapEntry{ at, at * static_cast<std::uint32_t>(sizeof(std::uint32_t)),
                    sizeof(std::uint32_t) };

            mInfo = VkSpecializationInfo{
                .mapEntryCount = static_cast<std::uint32_t>(mEntries.size()),
                .pMapEntries = mEntries.data(),
                .dataSize = mWords.size() * sizeof(std::uint32_t),
                .pData = mWords.data(),
            };
        }

        Specialization(const Specialization&) = delete;
        Specialization& operator=(const Specialization&) = delete;

        /// What a stage's `pSpecializationInfo` takes, or null where nothing was specialized.
        const VkSpecializationInfo* getInfo() const { return mEntries.empty() ? nullptr : &mInfo; }

    private:
        std::vector<std::uint32_t> mWords;
        std::vector<VkSpecializationMapEntry> mEntries;
        VkSpecializationInfo mInfo{};
    };

    /// What the three kinds of pipeline share: the handle, the layout descriptors are pushed
    /// against and push constants written through, and the bind point the two are addressed at.
    /// One object with its layout because they fail as one: a constructor that throws gets no
    /// destructor, so a pass that made these itself left a layout behind for `vkDestroyDevice` to
    /// find.
    class Pipeline
    {
    public:
        VkPipeline getHandle() const { return mHandle.get(); }
        VkPipelineLayout getLayout() const { return mLayout.getHandle(); }
        VkPipelineBindPoint getBindPoint() const { return mBindPoint; }
        const VkPushConstantRange& getPushRange() const { return mLayout.getPushRange(); }
        std::uint32_t getSetCount() const { return mLayout.getSetCount(); }

    protected:
        Pipeline(PipelineLayout&& layout, VkPipelineBindPoint bindPoint)
            : mLayout(std::move(layout))
            , mBindPoint(bindPoint)
        {
        }

        PipelineLayout mLayout;
        Owned<VkPipeline, vkDestroyPipeline> mHandle;

    private:
        VkPipelineBindPoint mBindPoint;
    };

    inline void bind(VkCommandBuffer commands, const Pipeline& pipeline)
    {
        vkCmdBindPipeline(commands, pipeline.getBindPoint(), pipeline.getHandle());
    }

    /// Pushes `SET_PASS`, which every pipeline here declares as a push descriptor set.
    inline void pushDescriptors(
        VkCommandBuffer commands, const Pipeline& pipeline, std::span<const VkWriteDescriptorSet> writes)
    {
        vkCmdPushDescriptorSet(commands, pipeline.getBindPoint(), pipeline.getLayout(), Shaders::SET_PASS,
            static_cast<std::uint32_t>(writes.size()), writes.data());
    }

    /// Binds every shared set the layout names, each at its own number: a layout is handed exactly
    /// the sets it was made with, and a bind short of one is a validation error at best.
    inline void bindSets(VkCommandBuffer commands, const Pipeline& pipeline, const SharedSetBinds& shared)
    {
        constexpr std::uint32_t first = Shaders::SET_PASS + 1;
        const std::array<VkDescriptorSet, Shaders::SET_COUNT> sets = shared.byNumber();
        const std::uint32_t count = pipeline.getSetCount();
        assert(count > first && "a bind for a pipeline that reads no shared set");
        for (std::uint32_t set = first; set < Shaders::SET_COUNT; ++set)
            assert((sets[set] != VK_NULL_HANDLE) == (set < count) && "a bind of other sets than the layout names");

        vkCmdBindDescriptorSets(commands, pipeline.getBindPoint(), pipeline.getLayout(), first, count - first,
            sets.data() + first, 0, nullptr);
    }

    /// Writes the whole push range, to the stages the layout declared it for. The size is the
    /// layout's, so a struct that grew on one side and not the other is caught here rather than
    /// read as garbage past the end of what was pushed. Copied by Vulkan before this returns.
    template <class Constants>
    void pushConstants(VkCommandBuffer commands, const Pipeline& pipeline, const Constants& constants)
    {
        const VkPushConstantRange& range = pipeline.getPushRange();
        assert(sizeof(Constants) == range.size && "push constants of a size the layout did not declare");

        vkCmdPushConstants(commands, pipeline.getLayout(), range.stageFlags, 0, range.size, &constants);
    }
}
