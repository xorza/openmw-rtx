#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "defs.hpp"
#include "path.hpp"
#include "refid.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    /*
     * Attribute definitions
     */

    struct Attribute
    {
        constexpr static RecNameInts sRecordId = REC_ATTR;
        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Attribute"; }

        static const RefId Strength;
        static const RefId Intelligence;
        static const RefId Willpower;
        static const RefId Agility;
        static const RefId Speed;
        static const RefId Endurance;
        static const RefId Personality;
        static const RefId Luck;
        static constexpr std::int32_t Length = 8;

        RefId mId;
        std::string mName, mDescription;
        Path mIcon;
        float mWerewolfValue{};

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();

        static RefId indexToRefId(std::int32_t index);
        static std::int32_t refIdToIndex(RefId id);
    };
}
