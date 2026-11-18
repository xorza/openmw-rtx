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
     * Global script variables
     */

    struct Global
    {
        constexpr static RecNameInts sRecordId = REC_GLOB;

        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Global"; }

        std::uint32_t mRecordFlags{};
        ESM::RefId mId;
        Variant mValue;

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID).
    };

    bool operator==(const Global& left, const Global& right);

}
