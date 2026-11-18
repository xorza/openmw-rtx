#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <utility>

#include <components/misc/hash.hpp>

namespace ESM
{
    class ESM3ExteriorCellRefId
    {
    public:
        constexpr ESM3ExteriorCellRefId() = default;

        constexpr explicit ESM3ExteriorCellRefId(std::int32_t x, std::int32_t y) noexcept
            : mX(x)
            , mY(y)
        {
        }

        std::string toString() const;

        std::string toDebugString() const;

        std::int32_t getX() const { return mX; }
        std::int32_t getY() const { return mY; }

        friend inline constexpr auto tie(const ESM3ExteriorCellRefId& value) noexcept
        {
            return std::tie(value.mX, value.mY);
        }

        constexpr bool operator==(ESM3ExteriorCellRefId rhs) const noexcept { return tie(*this) == tie(rhs); }

        constexpr bool operator<(ESM3ExteriorCellRefId rhs) const noexcept { return tie(*this) < tie(rhs); }

        friend std::ostream& operator<<(std::ostream& stream, ESM3ExteriorCellRefId value);

        friend struct std::hash<ESM3ExteriorCellRefId>;

    private:
        std::int32_t mX = 0;
        std::int32_t mY = 0;
    };
}

namespace std
{
    template <>
    struct hash<ESM::ESM3ExteriorCellRefId>
    {
        std::size_t operator()(ESM::ESM3ExteriorCellRefId value) const noexcept
        {
            return Misc::hash2dCoord(value.mX, value.mY);
        }
    };
}
