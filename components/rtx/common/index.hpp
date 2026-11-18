#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace Rtx
{
    /// No row of any table, as `std::nullopt` is no value of any optional: what a row is made at, assigned and
    /// compared against where it names none.
    struct NoRow
    {
        constexpr explicit NoRow(std::int32_t /*tag*/) {}
    };

    inline constexpr NoRow sNoRow{ 0 };

    /// **A row of one table, typed by it**: what the table hands out and takes back, so one table's row passes for no
    /// other's, as Chromium's `base::IdType` keeps one kind of id from another. Compared, ordered and hashed; its
    /// position (`at`) is what a vector kept beside the table is indexed by, and what a device's table is written at.
    /// Made at nothing, or at `sNoRow`, which no row is.
    ///
    /// @tparam Rows a tag naming the table, declared and never defined.
    template <class Rows>
    class RowIndex
    {
    public:
        /// No row.
        constexpr RowIndex() = default;

        /// The row at `at`, which a table hands out and a test names.
        constexpr explicit RowIndex(const std::uint32_t at)
            : mAt(at)
        {
        }

        /// No row, from `sNoRow`, as an optional is made from `std::nullopt`.
        // NOLINTNEXTLINE(google-explicit-constructor): implicit, as `std::optional`'s from `std::nullopt` is.
        constexpr RowIndex(NoRow /*none*/) {}

        constexpr bool isNone() const { return mAt == sNone; }

        /// Where the row stands: what a vector beside its table, or a device's copy of it, is indexed at.
        constexpr std::uint32_t at() const { return mAt; }

        friend constexpr bool operator==(RowIndex, RowIndex) = default;
        friend constexpr bool operator==(const RowIndex index, NoRow /*none*/) { return index.isNone(); }
        friend constexpr auto operator<=>(RowIndex, RowIndex) = default;

    private:
        static constexpr std::uint32_t sNone = ~std::uint32_t{ 0 };

        std::uint32_t mAt = sNone;
    };

    /// The scene's tables, each a tag its rows are typed by (`SceneDesc`).
    struct MeshRows;
    struct MaterialRows;
    struct TextureRows;
    struct PlacementRows;
    struct DeformerRows;
    struct LightCellRows;

    using MeshIndex = RowIndex<MeshRows>;
    using MaterialIndex = RowIndex<MaterialRows>;
    using TextureIndex = RowIndex<TextureRows>;
    using PlacementIndex = RowIndex<PlacementRows>;
    using DeformerIndex = RowIndex<DeformerRows>;
    using LightCellIndex = RowIndex<LightCellRows>;
}

template <class Rows>
struct std::hash<Rtx::RowIndex<Rows>>
{
    std::size_t operator()(const Rtx::RowIndex<Rows> index) const noexcept
    {
        return std::hash<std::uint32_t>{}(index.at());
    }
};
