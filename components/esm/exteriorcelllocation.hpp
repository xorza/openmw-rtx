#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <tuple>

#include <components/esm3/loadcell.hpp>

#include "refid.hpp"

namespace ESM
{
    struct ExteriorCellLocation
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;
        ESM::RefId mWorldspace = ESM::Cell::sDefaultWorldspaceId;

        ExteriorCellLocation() = default;

        ExteriorCellLocation(std::int32_t x, std::int32_t y, ESM::RefId worldspace)
            : mX(x)
            , mY(y)
            , mWorldspace(worldspace)
        {
        }

        friend bool operator==(const ExteriorCellLocation& lhs, const ExteriorCellLocation& rhs) = default;

        friend inline bool operator<(const ExteriorCellLocation& lhs, const ExteriorCellLocation& rhs)
        {
            return std::make_tuple(lhs.mX, lhs.mY, lhs.mWorldspace) < std::make_tuple(rhs.mX, rhs.mY, rhs.mWorldspace);
        }

        friend inline std::ostream& operator<<(std::ostream& stream, const ExteriorCellLocation& value)
        {
            return stream << "{" << value.mX << ", " << value.mY << ", " << value.mWorldspace << "}";
        }
    };
}

namespace std
{
    template <>
    struct hash<ESM::ExteriorCellLocation>
    {
        std::size_t operator()(const ESM::ExteriorCellLocation& toHash) const
        {
            // Compute individual hash values for first,
            // second and third and combine them using XOR
            // and bit shifting:

            return ((hash<std::int32_t>()(toHash.mX) ^ (hash<std::int32_t>()(toHash.mY) << 1)) >> 1)
                ^ (hash<ESM::RefId>()(toHash.mWorldspace) << 1);
        }
    };
}
