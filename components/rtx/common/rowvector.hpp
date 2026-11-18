#pragma once

#include <cassert>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "index.hpp"
#include "rowspan.hpp"

namespace Rtx
{
    /// **What is kept beside a table, a value a row**, indexed by the table's rows (`RowIndex`) and no other's: a
    /// mesh's digest, a material's asks, a slot's word on the device. Grown by its owner as the table grows.
    ///
    /// @tparam T what is kept for each row.
    /// @tparam I the table's row.
    template <class T, class I>
    class RowVector
    {
    public:
        T& operator[](const I row)
        {
            assert(row.at() < mItems.size() && "a row the vector has not grown to");
            return mItems[row.at()];
        }

        const T& operator[](const I row) const
        {
            assert(row.at() < mItems.size() && "a row the vector has not grown to");
            return mItems[row.at()];
        }

        /// Whether `row` stands inside what is kept.
        bool holds(const I row) const { return !row.isNone() && row.at() < mItems.size(); }

        /// Grows to hold `row`, the rows past what it held made as a `T` is.
        void growTo(const I row)
        {
            if (row.at() >= mItems.size())
                mItems.resize(std::size_t{ row.at() } + 1);
        }

        /// Grows to hold `row`, the rows past what it held made at `fill`.
        void growTo(const I row, const T& fill)
        {
            if (row.at() >= mItems.size())
                mItems.resize(std::size_t{ row.at() } + 1, fill);
        }

        void resize(const std::size_t count) { mItems.resize(count); }
        void resize(const std::size_t count, const T& fill) { mItems.resize(count, fill); }
        void reserve(const std::size_t count) { mItems.reserve(count); }
        void clear() { mItems.clear(); }
        void push_back(T value) { mItems.push_back(std::move(value)); }
        void assign(const std::size_t count, const T& value) { mItems.assign(count, value); }

        std::size_t size() const { return mItems.size(); }
        bool empty() const { return mItems.empty(); }
        auto begin() { return mItems.begin(); }
        auto end() { return mItems.end(); }
        auto begin() const { return mItems.begin(); }
        auto end() const { return mItems.end(); }

        /// What is kept, by row, for a reader.
        RowSpan<const T, I> rows() const { return RowSpan<const T, I>(std::span<const T>(mItems)); }

        /// What is kept as it lies, value after value.
        std::span<const T> items() const { return mItems; }
        std::span<T> items() { return mItems; }

    private:
        std::vector<T> mItems;
    };
}
