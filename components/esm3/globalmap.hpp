#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include <components/esm/defs.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only

    ///< \brief An image containing the explored areas on the global map.
    struct GlobalMap
    {
        constexpr static RecNameInts sRecordId = REC_GMAP;

        // The minimum and maximum cell coordinates
        struct Bounds
        {
            std::int32_t mMinX, mMaxX, mMinY, mMaxY;
        };

        Bounds mBounds{};

        std::vector<char> mImageData;

        typedef std::pair<std::int32_t, std::int32_t> CellId;
        std::set<CellId> mMarkers;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

}
