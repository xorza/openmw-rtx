#pragma once

#include <string>

#include <components/esm/refid.hpp>
#include <components/esm3/formatversion.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace CSMWorld
{
    struct MetaData
    {
        static constexpr std::string_view getRecordType() { return "MetaData"; }

        ESM::RefId mId;

        ESM::FormatVersion mFormatVersion{};
        std::string mAuthor;
        std::string mDescription;

        void blank();

        void load(ESM::ESMReader& esm);
        void save(ESM::ESMWriter& esm) const;
    };
}
