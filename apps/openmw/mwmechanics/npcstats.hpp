#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadskil.hpp>

#include "creaturestats.hpp"

namespace ESM
{
    struct Class;
    struct NpcStats;
}

namespace MWMechanics
{
    /// \brief Additional stats for NPCs

    class NpcStats : public CreatureStats
    {
        std::int32_t mDisposition;
        std::int32_t mCrimeDispositionModifier;
        std::map<ESM::RefId, SkillValue> mSkills; // SkillValue.mProgress used by the player only

        std::int32_t mReputation;
        std::int32_t mCrimeId;

        // ----- used by the player only, maybe should be moved at some point -------
        std::int32_t mBounty;
        std::int32_t mWerewolfKills;
        /// Used only for the player and for NPC's with ranks, modified by scripts; other NPCs have maximum one faction
        /// defined in their NPC record
        std::map<ESM::RefId, std::int32_t> mFactionRank;
        std::set<ESM::RefId> mExpelled;
        std::map<ESM::RefId, std::int32_t> mFactionReputation;
        std::int32_t mLevelProgress; // 0-10
        std::map<ESM::RefId, std::int32_t>
            mSkillIncreases; // number of skill increases for each attribute (resets after leveling up)
        std::vector<std::int32_t> mSpecIncreases; // number of skill increases for each specialization (accumulates
                                                  // throughout the entire game)
        std::set<ESM::RefId> mUsedIds;
        // ---------------------------------------------------------------------------

        /// Countdown to getting damage while underwater
        float mTimeToStartDrowning;

        bool mIsWerewolf;

    public:
        NpcStats();

        std::int32_t getBaseDisposition() const;
        void setBaseDisposition(std::int32_t disposition);

        std::int32_t getCrimeDispositionModifier() const;
        void setCrimeDispositionModifier(std::int32_t value);
        void modCrimeDispositionModifier(std::int32_t value);

        std::int32_t getReputation() const;
        void setReputation(std::int32_t reputation);

        std::int32_t getCrimeId() const;
        void setCrimeId(std::int32_t id);

        const SkillValue& getSkill(ESM::RefId id) const;
        SkillValue& getSkill(ESM::RefId id);
        void setSkill(ESM::RefId id, const SkillValue& value);

        std::int32_t getFactionRank(const ESM::RefId& faction) const;
        const std::map<ESM::RefId, std::int32_t>& getFactionRanks() const;

        /// Join this faction, setting the initial rank to 0.
        void joinFaction(const ESM::RefId& faction);
        /// Sets the rank in this faction to a specified value, if such a rank exists.
        void setFactionRank(const ESM::RefId& faction, std::int32_t newRank);

        const std::set<ESM::RefId>& getExpelled() const { return mExpelled; }
        bool getExpelled(const ESM::RefId& factionID) const;
        void expell(const ESM::RefId& factionID, bool printMessage);
        void clearExpelled(const ESM::RefId& factionID);

        bool isInFaction(const ESM::RefId& faction) const;

        float getSkillProgressRequirement(ESM::RefId id, const ESM::Class& npcClass) const;

        std::int32_t getLevelProgress() const;
        void setLevelProgress(std::int32_t progress);

        std::int32_t getLevelupAttributeMultiplier(ESM::RefId attribute) const;
        std::int32_t getSkillIncreasesForAttribute(ESM::RefId attribute) const;
        void setSkillIncreasesForAttribute(ESM::RefId, std::int32_t increases);

        std::int32_t getSkillIncreasesForSpecialization(ESM::Class::Specialization spec) const;
        void setSkillIncreasesForSpecialization(ESM::Class::Specialization spec, std::int32_t increases);

        void levelUp();

        void updateHealth();
        ///< Calculate health based on endurance and strength.
        ///  Called at character creation.

        void flagAsUsed(const ESM::RefId& id);
        ///< @note Id must be lower-case

        bool hasBeenUsed(const ESM::RefId& id) const;
        ///< @note Id must be lower-case

        std::int32_t getBounty() const;

        void setBounty(std::int32_t bounty);

        std::int32_t getFactionReputation(const ESM::RefId& faction) const;

        void setFactionReputation(const ESM::RefId& faction, std::int32_t value);

        bool hasSkillsForRank(const ESM::RefId& factionId, std::int32_t rank) const;

        bool isWerewolf() const;

        void setWerewolf(bool set);

        std::int32_t getWerewolfKills() const;

        /// Increments mWerewolfKills by 1.
        void addWerewolfKill();

        float getTimeToStartDrowning() const;
        /// Sets time left for the creature to drown if it stays underwater.
        /// @param time value from [0,20]
        void setTimeToStartDrowning(float time);

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void writeState(ESM::CreatureStats& state) const;
        void writeState(ESM::NpcStats& state) const;

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void readState(const ESM::CreatureStats& state);
        void readState(const ESM::NpcStats& state);

        const std::map<ESM::RefId, SkillValue>& getSkills() const { return mSkills; }
    };
}
