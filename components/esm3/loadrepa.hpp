#pragma once

#include <cstdint>
#include <string>

#include <components/esm/defs.hpp>
#include <components/esm/path.hpp>
#include <components/esm/refid.hpp>

namespace ESM
{

    class ESMReader;
    class ESMWriter;

    struct Repair
    {
        constexpr static RecNameInts sRecordId = REC_REPA;

        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Repair"; }

        struct Data
        {
            float mWeight;
            std::int32_t mValue;

            std::int32_t mUses;
            float mQuality;
        }; // Size = 16

        Data mData{};
        std::uint32_t mRecordFlags{};
        RefId mId, mScript;
        std::string mName;
        Path mModel;
        Path mIcon;

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID).
    };

}
