#pragma once

#include <cstdint>
#include <string>

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    struct DebugProfile
    {
        constexpr static RecNameInts sRecordId = REC_DBGP;

        static constexpr std::string_view getRecordType() { return "DebugProfile"; }

        enum Flags
        {
            Flag_Default = 1, // add to newly opened scene subviews
            Flag_BypassNewGame = 2, // bypass regular game startup
            Flag_Global = 4 // make available from main menu (i.e. not location specific)
        };

        std::uint32_t mRecordFlags{};
        RefId mId;

        std::string mDescription;

        std::string mScriptText;

        std::uint32_t mFlags{};

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        /// Set record to default state (does not touch the ID).
        void blank();
    };
}
