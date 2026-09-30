#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/shaders/sets.h>
#include <components/rtxvulkan/device/bindingtable.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/owned.hpp>

namespace Rtx
{
    class Device;

    /// What a pipeline that is pushed nothing is pushed: its constants moved into a buffer, or it
    /// never had any.
    struct NoConstants
    {
    };

    /// The push range `Constants` needs, whole: nought for `NoConstants`, because Vulkan takes no
    /// empty range.
    template <class Constants>
    constexpr std::uint32_t pushBytesOf()
    {
        if constexpr (std::is_same_v<Constants, NoConstants>)
            return 0;
        else
            return sizeof(Constants);
    }

    /// The map entries a table of specialization words needs, and the `VkSpecializationInfo` over
    /// them. Built here rather than by the caller, because it is the same table every time: a table
    /// is indexed by `constant_id`, which the shared headers name (`SPEC_*`), so `constant_id` `i`
    /// takes word `i`, at word `i`'s offset. Words because that is what every constant this
    /// renderer specializes on is — a `bool` reaches SPIR-V as a 32-bit value like a `uint` does.
    /// The words are copied, so nothing outlives the object but what `getInfo` points at, which is
    /// the object's own.
    class Specialization
    {
    public:
        explicit Specialization(std::span<const std::uint32_t> words)
            : mWords(words.begin(), words.end())
        {
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

    /// What making every kind of pipeline shares, for as long as one is being made: the crash
    /// note that names it, the statistics asked for at creation because they cannot be asked for
    /// afterwards, and the creation feedback that says whether the driver compiled it or found it
    /// cached. Afterwards the pipeline is named and reported, with its compile time where there is
    /// one.
    class PipelineCreation
    {
    public:
        /// What every create info's `flags` starts from. The cost is to compiling the pipeline and
        /// not to running it, and every pipeline here is made once; the answer it buys is the only
        /// way to see a register count, which is what an occupancy figure is made of.
        static constexpr VkPipelineCreateFlags sFlags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;

        PipelineCreation(const Device& device, std::string_view name);

        PipelineCreation(const PipelineCreation&) = delete;
        PipelineCreation& operator=(const PipelineCreation&) = delete;

        /// What a create info's `pNext` points at: the feedback, and `next` after it.
        const void* chain(const void* next);

        /// Names `pipeline` and reports it. Its compile time is reported only where the driver
        /// compiled it: a pipeline the application's cache handed back took no compile at all.
        void finish(VkPipeline pipeline) const;

    private:
        const Device& mDevice;
        std::string_view mName;
        Crash::NoteScope mNoted;
        VkPipelineCreationFeedback mFeedback{};
        VkPipelineCreationFeedbackCreateInfo mTimed{};
    };

    /// What the three kinds of pipeline share: the handle, the layout descriptors are pushed
    /// against and push constants written through, and the bind point the two are addressed at.
    /// One object with its layout because they fail as one: the handle is made against the
    /// layout before either is handed here, so a handle that cannot be made leaves the layout to
    /// the caller's unwind rather than behind for `vkDestroyDevice` to find.
    class Pipeline
    {
    public:
        VkPipeline getHandle() const { return mHandle.get(); }
        VkPipelineLayout getLayout() const { return mLayout.getHandle(); }
        VkPipelineBindPoint getBindPoint() const { return mBindPoint; }
        const VkPushConstantRange& getPushRange() const { return mLayout.getPushRange(); }
        std::uint32_t getSetCount() const { return mLayout.getSetCount(); }
        const BindingTable& getBindings() const { return mLayout.getBindings(); }

    protected:
        Pipeline(PipelineLayout&& layout, Owned<VkPipeline, vkDestroyPipeline>&& handle, VkPipelineBindPoint bindPoint)
            : mLayout(std::move(layout))
            , mHandle(std::move(handle))
            , mBindPoint(bindPoint)
        {
        }

    private:
        PipelineLayout mLayout;
        Owned<VkPipeline, vkDestroyPipeline> mHandle;
        VkPipelineBindPoint mBindPoint;
    };

    /// A pipeline pushed a `Constants` and nothing else: the push range is the type's size, so a
    /// pass cannot push another struct, whatever its size. What the three kinds share once their
    /// constants are known.
    template <class Constants>
    class TypedPipeline : public Pipeline
    {
    public:
        /// Writes the whole range, to the stages the layout declared it for. Copied by Vulkan
        /// before this returns.
        void push(VkCommandBuffer commands, const Constants& constants) const
            requires(!std::is_same_v<Constants, NoConstants>)
        {
            const VkPushConstantRange& range = getPushRange();
            vkCmdPushConstants(commands, getLayout(), range.stageFlags, 0, range.size, &constants);
        }

    protected:
        TypedPipeline(
            PipelineLayout&& layout, Owned<VkPipeline, vkDestroyPipeline>&& handle, VkPipelineBindPoint bindPoint)
            : Pipeline(std::move(layout), std::move(handle), bindPoint)
        {
        }
    };

    inline void bind(VkCommandBuffer commands, const Pipeline& pipeline)
    {
        vkCmdBindPipeline(commands, pipeline.getBindPoint(), pipeline.getHandle());
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
}
