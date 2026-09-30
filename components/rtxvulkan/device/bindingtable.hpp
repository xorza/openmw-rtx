#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// The bindings a descriptor set layout was made from, in binding order: the one statement of
    /// each binding's number, type and count, which a write takes its type from and is checked
    /// against. Kept by value in a fixed array, because a layout is made at start-up and read at
    /// every write, and a write is on the frame path.
    class BindingTable
    {
    public:
        /// The most bindings any set of this renderer declares, with room: the fog volume's set has
        /// twenty.
        static constexpr std::size_t sMost = 24;

        BindingTable() = default;

        explicit BindingTable(std::span<const VkDescriptorSetLayoutBinding> bindings)
            : mCount(bindings.size())
        {
            assert(bindings.size() <= sMost && "a set of more bindings than a table holds");
            for (std::size_t at = 0; at < bindings.size(); ++at)
            {
                assert((at == 0 || bindings[at - 1].binding < bindings[at].binding)
                    && "a set's bindings out of binding order");
                mBindings[at] = bindings[at];
            }
        }

        std::span<const VkDescriptorSetLayoutBinding> get() const { return { mBindings.data(), mCount }; }

    private:
        std::array<VkDescriptorSetLayoutBinding, sMost> mBindings{};
        std::size_t mCount = 0;
    };
}
