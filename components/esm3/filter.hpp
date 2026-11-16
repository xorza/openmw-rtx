#pragma once

#include <string>

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    struct Filter
    {
        constexpr static RecNameInts sRecordId = REC_FILT;

        static constexpr std::string_view getRecordType() { return "Filter"; }

        uint32_t mRecordFlags{};
        RefId mId;

        std::string mDescription;

        std::string mFilter;

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID).
    };
}
