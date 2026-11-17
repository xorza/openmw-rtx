#pragma once

#include <cstdint>
#include <string>

#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only

    struct JournalEntry
    {
        enum Type
        {
            Type_Journal = 0,
            Type_Topic = 1,
            Type_Quest = 2
        };

        std::int32_t mType{};
        ESM::RefId mTopic;
        ESM::RefId mInfo;
        std::string mText;
        std::string mActorName; // Could also be Actor ID to allow switching of localisation, but since mText is
                                // plaintext anyway...
        std::int32_t mDay{}; // time stamp
        std::int32_t mMonth{};
        std::int32_t mDayOfMonth{};

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}
