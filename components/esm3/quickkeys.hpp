#pragma once

#include <string>
#include <vector>

#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    struct QuickKeys
    {
        enum class Type : std::uint32_t
        {
            Item = 0,
            Magic = 1,
            MagicItem = 2,
            Unassigned = 3,
            HandToHand = 4,
        };

        struct QuickKey
        {
            Type mType{};
            RefId mId; // Spell or Item ID
        };

        std::vector<QuickKey> mKeys;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

}
