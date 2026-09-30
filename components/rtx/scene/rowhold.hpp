#pragma once

#include <cassert>
#include <exception>
#include <utility>

#include <components/rtx/common/runs.hpp>

namespace Rtx
{
    class SceneDesc;

    /// One hold on one row of a scene table, taken by the `SceneDesc` call that answers it and
    /// given back by `SceneDesc::drop`, which empties it. Nothing else can make one or empty one,
    /// so a row's count is the number of holds standing on it.
    ///
    /// **Move-only**, so a hold is given back once: a second drop of the same hold finds it empty
    /// and changes no count, where a bare index given back twice took a hold another holder still
    /// counted on. **Asserted empty where it goes**, so a hold its holder forgot is named at the
    /// holder and not found as a row that never leaves. Not while an exception unwinds, where the
    /// frame's own message is the one to read.
    ///
    /// @tparam Row the table's row, which is what keeps a mesh's hold from being given back as a
    ///         material's.
    template <class Row>
    class Hold
    {
    public:
        Hold() = default;

        ~Hold()
        {
            assert((mIndex == sNoIndex || std::uncaught_exceptions() > 0) && "a hold on a scene row nothing gave back");
        }

        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;

        Hold(Hold&& other) noexcept
            : mIndex(std::exchange(other.mIndex, sNoIndex))
        {
        }

        Hold& operator=(Hold&& other) noexcept
        {
            assert((this == &other || mIndex == sNoIndex) && "a hold written over while it still holds");
            mIndex = std::exchange(other.mIndex, sNoIndex);
            return *this;
        }

        /// The row held, or `sNoIndex` for a hold of nothing.
        Index get() const { return mIndex; }

        bool empty() const { return mIndex == sNoIndex; }

    private:
        friend class SceneDesc;

        explicit Hold(Index index)
            : mIndex(index)
        {
        }

        Index release() { return std::exchange(mIndex, sNoIndex); }

        Index mIndex = sNoIndex;
    };

    struct MeshRange;
    struct Material;
    struct TextureRow;

    using MeshHold = Hold<MeshRange>;
    using MaterialHold = Hold<Material>;
    using TextureHold = Hold<TextureRow>;
}
