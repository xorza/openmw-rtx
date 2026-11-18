#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

namespace ESM
{

    class ESMReader;
    class ESMWriter;

    /*
     * Faction definitions
     */

    // Requirements for each rank
    struct RankData
    {
        std::int32_t mAttribute1, mAttribute2; // Attribute level

        // Skill level (faction skills given in
        // skillID below.) You need one skill at
        // level 'mPrimarySkill' and two skills at level
        // 'mFavouredSkill' to advance to this rank.
        std::int32_t mPrimarySkill, mFavouredSkill;

        std::int32_t mFactReputation; // Required faction rep

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;

        void blank();
    };

    struct Faction
    {
        constexpr static RecNameInts sRecordId = REC_FACT;

        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Faction"; }

        std::uint32_t mRecordFlags{};
        std::string mName;
        RefId mId;

        enum Flags
        {
            Hidden = 1
        };

        struct FADTstruct
        {
            // Which attributes we like
            std::array<ESM::RefId, 2> mAttribute;

            std::array<RankData, 10> mRankData{};

            std::array<ESM::RefId, 7> mSkills; // IDs of skills this faction requires

            std::int32_t mFlags{}; // 1 - hidden from player

            ESM::RefId& getSkill(std::size_t index, bool ignored = false);
            ///< Throws an exception for invalid values of \a index.

            ESM::RefId getSkill(std::size_t index, bool ignored = false) const;
            ///< Throws an exception for invalid values of \a index.

            void load(ESMReader& esm);
            void save(ESMWriter& esm) const;
        }; // 240 bytes

        FADTstruct mData;

        // <Faction ID, Reaction>
        std::map<ESM::RefId, std::int32_t> mReactions;

        // Name of faction ranks (may be empty for NPC factions)
        std::array<std::string, 10> mRanks;

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID/index).
    };
}
