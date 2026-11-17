#pragma once

#include <cstdint>
#include <string>

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

#include "variant.hpp"

namespace ESM
{

    class ESMReader;
    class ESMWriter;

    /*
     *  Game setting
     *
     */

    struct GameSetting
    {
        constexpr static RecNameInts sRecordId = REC_GMST;

        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "GameSetting"; }

        std::uint32_t mRecordFlags{};
        RefId mId;

        Variant mValue;

        void load(ESMReader& esm, bool& isDeleted);

        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID).
    };

    bool operator==(const GameSetting& left, const GameSetting& right);
}
