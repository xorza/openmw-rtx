#pragma once

#include <cassert>
#include <cstdint>

#include <components/rtx/common/index.hpp>

namespace Rtx
{
    /// Which scene a call is about: the world, or one a view asked for. The world is a case and
    /// not a sentinel, because a `~0u` in the same value space as the indices `addViewScene` hands
    /// out is a view table indexed out of range by a caller that forgot it.
    class SceneSlot
    {
    public:
        constexpr SceneSlot() = default;

        static constexpr SceneSlot world() { return SceneSlot{}; }

        static constexpr SceneSlot view(std::uint32_t index)
        {
            assert(index != sWorldIndex && "a view scene at the one value that names the world");
            return SceneSlot{ index };
        }

        constexpr bool isWorld() const { return mIndex == sWorldIndex; }

        constexpr std::uint32_t getViewIndex() const
        {
            assert(!isWorld() && "the world asked for the index of a view");
            return mIndex;
        }

        constexpr bool operator==(const SceneSlot& other) const = default;

    private:
        static constexpr std::uint32_t sWorldIndex = ~std::uint32_t{ 0 };

        constexpr explicit SceneSlot(std::uint32_t index)
            : mIndex(index)
        {
        }

        std::uint32_t mIndex = sWorldIndex;
    };

    /// One texture the GUI draws from, or nothing. Nothing is a state and not an index, because a
    /// MyGUI texture exists before it is given one: `createManual` is what takes the slot, and
    /// everything before that has to answer.
    using GuiSlot = RowIndex<struct GuiRows>;
}
