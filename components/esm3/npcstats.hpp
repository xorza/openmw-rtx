#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <components/esm/attr.hpp>
#include <components/esm/refid.hpp>

#include "loadskil.hpp"
#include "statstate.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only

    struct NpcStats
    {
        struct Faction
        {
            bool mExpelled;
            std::int32_t mRank;
            std::int32_t mReputation;

            Faction();
        };

        bool mIsWerewolf{};

        std::map<ESM::RefId, Faction> mFactions;
        std::int32_t mDisposition{};
        std::int32_t mCrimeDispositionModifier{};
        std::map<ESM::RefId, StatState<float>> mSkills;
        std::int32_t mBounty{};
        std::int32_t mReputation{};
        std::int32_t mWerewolfKills{};
        std::int32_t mLevelProgress{};
        std::map<ESM::RefId, std::int32_t> mSkillIncrease;
        std::array<std::int32_t, 3> mSpecIncreases{};
        std::vector<ESM::RefId> mUsedIds;
        float mTimeToStartDrowning{};
        std::int32_t mCrimeId{};

        /// Initialize to default state
        void blank();

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}
