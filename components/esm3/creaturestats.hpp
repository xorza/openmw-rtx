#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <components/esm/attr.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

#include "activespells.hpp"
#include "aisequence.hpp"
#include "magiceffects.hpp"
#include "spellstate.hpp"
#include "statstate.hpp"
#include "timestamp.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only
    struct CreatureStats
    {
        struct CorprusStats
        {
            std::array<std::int32_t, Attribute::Length> mWorsenings;
            TimeStamp mNextWorsening;
        };

        std::map<ESM::RefId, StatState<float>> mAttributes;
        std::array<StatState<float>, 3> mDynamic;

        MagicEffects mMagicEffects;

        AiSequence::AiSequence mAiSequence;

        bool mHasAiSettings{};
        std::array<StatState<std::int32_t>, 4> mAiSettings;

        std::map<SummonKey, std::int32_t> mSummonedCreatureMap;
        std::multimap<ESM::RefId, RefNum> mSummonedCreatures;
        std::vector<std::int32_t> mSummonGraveyard;

        TimeStamp mTradeTime{};
        std::int32_t mGoldPool{};
        std::int32_t mActorId{};

        enum Flags
        {
            Dead = 0x0001,
            DeathAnimationFinished = 0x0002,
            Died = 0x0004,
            Murdered = 0x0008,
            TalkedTo = 0x0010,
            Alarmed = 0x0020,
            Attacked = 0x0040,
            Knockdown = 0x0080,
            KnockdownOneFrame = 0x0100,
            KnockdownOverOneFrame = 0x0200,
            HitRecovery = 0x0400,
            Block = 0x0800,
            RecalcDynamicStats = 0x1000
        };
        bool mDead{};
        bool mDeathAnimationFinished{};
        bool mDied{};
        bool mMurdered{};
        bool mTalkedTo{};
        bool mAlarmed{};
        bool mAttacked{};
        bool mKnockdown{};
        bool mKnockdownOneFrame{};
        bool mKnockdownOverOneFrame{};
        bool mHitRecovery{};
        bool mBlock{};
        std::uint32_t mMovementFlags{};
        float mFallHeight{};
        ESM::RefId mLastHitObject;
        ESM::RefId mLastHitAttemptObject;
        bool mRecalcDynamicStats{};
        std::int32_t mDrawState{};
        std::int8_t mDeathAnimation{};
        TimeStamp mTimeOfDeath{};
        std::int32_t mLevel{};
        bool mMissingACDT{};

        std::map<ESM::RefId, CorprusStats> mCorprusSpells;
        SpellState mSpells;
        ActiveSpells mActiveSpells;

        /// Initialize to default state
        void blank();

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}
