#pragma once

#include <cassert>
#include <exception>
#include <utility>

#include <components/rtx/common/holdtier.hpp>
#include <components/rtx/common/index.hpp>

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
    /// @tparam I its index (`RowIndex`).
    ///
    /// **It states its tier** (`HoldTier`), which its row is counted at while it stands and given back at.
    template <class Row, class I>
    class Hold
    {
    public:
        Hold() = default;

        ~Hold()
        {
            assert((mIndex == sNoRow || std::uncaught_exceptions() > 0) && "a hold on a scene row nothing gave back");
        }

        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;

        Hold(Hold&& other) noexcept
            : mIndex(std::exchange(other.mIndex, sNoRow))
            , mTier(other.mTier)
        {
        }

        Hold& operator=(Hold&& other) noexcept
        {
            assert((this == &other || mIndex == sNoRow) && "a hold written over while it still holds");
            mIndex = std::exchange(other.mIndex, sNoRow);
            mTier = other.mTier;
            return *this;
        }

        /// The row held, or `sNoRow` for a hold of nothing.
        I get() const { return mIndex; }

        bool empty() const { return mIndex == sNoRow; }

        /// The tier it states.
        HoldTier getTier() const { return mTier; }

    private:
        friend class SceneDesc;

        Hold(const I index, const HoldTier tier)
            : mIndex(index)
            , mTier(tier)
        {
        }

        I release() { return std::exchange(mIndex, sNoRow); }

        I mIndex = sNoRow;
        HoldTier mTier = HoldTier::Unheld;
    };

    struct MeshRange;
    struct Material;
    struct TextureRow;

    /// What a hold on a texture its holder reads itself is kept apart by: a sky sheet, a sprite, a sprite's light.
    struct TextureRead;

    using MeshHold = Hold<MeshRange, MeshIndex>;
    using MaterialHold = Hold<Material, MaterialIndex>;
    using TextureHold = Hold<TextureRow, TextureIndex>;

    /// **A hold on a texture its holder reads itself**, which asks its finest level for as long as it stands
    /// (`SceneDesc::takeReading`). A `TextureHold` asks nothing: what a material names is asked by the placements
    /// wearing it, and the identity a walk met a texture under reads nothing.
    using ReadingHold = Hold<TextureRead, TextureIndex>;
}
