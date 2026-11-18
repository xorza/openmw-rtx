#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

#include "index.hpp"

namespace Rtx
{
    /// **What a table holds, read by its rows** (`RowIndex`): a span a row of the table indexes and no other's, as a
    /// table's own rows are what its readers walk. `items` is the span as it lies, for a copy of the whole.
    ///
    /// @tparam T what stands at each row, `const` for a reader.
    /// @tparam I the table's row.
    template <class T, class I>
    class RowSpan
    {
    public:
        constexpr RowSpan() = default;

        constexpr explicit RowSpan(const std::span<T> items)
            : mItems(items)
        {
        }

        constexpr T& operator[](const I row) const
        {
            assert(row.at() < mItems.size() && "a row past the table");
            return mItems[row.at()];
        }

        /// Whether `row` stands inside the table: what a row from elsewhere is checked against.
        constexpr bool holds(const I row) const { return !row.isNone() && row.at() < mItems.size(); }

        constexpr std::size_t size() const { return mItems.size(); }
        constexpr bool empty() const { return mItems.empty(); }
        constexpr auto begin() const { return mItems.begin(); }
        constexpr auto end() const { return mItems.end(); }

        /// The span as it lies, row after row.
        constexpr std::span<T> items() const { return mItems; }

    private:
        std::span<T> mItems;
    };

    /// The rows from `first` up to `end`, in order: what a walk of a table's newest rows goes through.
    template <class I>
    constexpr auto rowsBetween(const std::size_t first, const std::size_t end)
    {
        assert(first <= end && "a range of rows that ends before it starts");
        return std::views::iota(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(end))
            | std::views::transform([](const std::uint32_t at) { return I(at); });
    }

    /// Every row of a table `count` long, in order: what a walk of the whole table goes through.
    template <class I>
    constexpr auto everyRow(const std::size_t count)
    {
        return rowsBetween<I>(0, count);
    }
}
