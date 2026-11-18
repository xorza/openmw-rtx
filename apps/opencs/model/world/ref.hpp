#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <components/esm3/cellref.hpp>

namespace CSMWorld
{
    /// \brief Wrapper for CellRef sub record
    struct CellRef : public ESM::CellRef
    {
        ESM::RefId mId;
        ESM::RefId mCell;
        ESM::RefId mOriginalCell;
        bool mNew; // new reference, not counted yet, ref num not assigned yet
        std::uint32_t mIdNum;

        CellRef();

        /// Calculate cell index based on coordinates (x and y)
        std::pair<std::int32_t, std::int32_t> getCellIndex() const;
    };
}
