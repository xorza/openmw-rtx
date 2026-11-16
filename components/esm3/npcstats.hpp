#pragma once

#include <array>
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
            int32_t mRank;
            int32_t mReputation;

            Faction();
        };

        bool mIsWerewolf{};

        std::map<ESM::RefId, Faction> mFactions;
        int32_t mDisposition{};
        int32_t mCrimeDispositionModifier{};
        std::map<ESM::RefId, StatState<float>> mSkills;
        int32_t mBounty{};
        int32_t mReputation{};
        int32_t mWerewolfKills{};
        int32_t mLevelProgress{};
        std::map<ESM::RefId, int32_t> mSkillIncrease;
        std::array<int32_t, 3> mSpecIncreases{};
        std::vector<ESM::RefId> mUsedIds;
        float mTimeToStartDrowning{};
        int32_t mCrimeId{};

        /// Initialize to default state
        void blank();

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}
