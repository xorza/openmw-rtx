#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    struct DialogueCondition
    {
        enum Function : std::int8_t
        {
            Function_FacReactionLowest = 0,
            Function_FacReactionHighest = 1,
            Function_RankRequirement = 2,
            Function_Reputation = 3,
            Function_Health_Percent = 4,
            Function_PcReputation = 5,
            Function_PcLevel = 6,
            Function_PcHealthPercent = 7,
            Function_PcMagicka = 8,
            Function_PcFatigue = 9,
            Function_PcStrength = 10,
            Function_PcBlock = 11,
            Function_PcArmorer = 12,
            Function_PcMediumArmor = 13,
            Function_PcHeavyArmor = 14,
            Function_PcBluntWeapon = 15,
            Function_PcLongBlade = 16,
            Function_PcAxe = 17,
            Function_PcSpear = 18,
            Function_PcAthletics = 19,
            Function_PcEnchant = 20,
            Function_PcDestruction = 21,
            Function_PcAlteration = 22,
            Function_PcIllusion = 23,
            Function_PcConjuration = 24,
            Function_PcMysticism = 25,
            Function_PcRestoration = 26,
            Function_PcAlchemy = 27,
            Function_PcUnarmored = 28,
            Function_PcSecurity = 29,
            Function_PcSneak = 30,
            Function_PcAcrobatics = 31,
            Function_PcLightArmor = 32,
            Function_PcShortBlade = 33,
            Function_PcMarksman = 34,
            Function_PcMercantile = 35,
            Function_PcSpeechcraft = 36,
            Function_PcHandToHand = 37,
            Function_PcGender = 38,
            Function_PcExpelled = 39,
            Function_PcCommonDisease = 40,
            Function_PcBlightDisease = 41,
            Function_PcClothingModifier = 42,
            Function_PcCrimeLevel = 43,
            Function_SameSex = 44,
            Function_SameRace = 45,
            Function_SameFaction = 46,
            Function_FactionRankDifference = 47,
            Function_Detected = 48,
            Function_Alarmed = 49,
            Function_Choice = 50,
            Function_PcIntelligence = 51,
            Function_PcWillpower = 52,
            Function_PcAgility = 53,
            Function_PcSpeed = 54,
            Function_PcEndurance = 55,
            Function_PcPersonality = 56,
            Function_PcLuck = 57,
            Function_PcCorprus = 58,
            Function_Weather = 59,
            Function_PcVampire = 60,
            Function_Level = 61,
            Function_Attacked = 62,
            Function_TalkedToPc = 63,
            Function_PcHealth = 64,
            Function_CreatureTarget = 65,
            Function_FriendHit = 66,
            Function_Fight = 67,
            Function_Hello = 68,
            Function_Alarm = 69,
            Function_Flee = 70,
            Function_ShouldAttack = 71,
            Function_Werewolf = 72,
            Function_PcWerewolfKills = 73,

            Function_Global = 74,
            Function_Local = 75,
            Function_Journal = 76,
            Function_Item = 77,
            Function_Dead = 78,
            Function_NotId = 79,
            Function_NotFaction = 80,
            Function_NotClass = 81,
            Function_NotRace = 82,
            Function_NotCell = 83,
            Function_NotLocal = 84,

            Function_None = 85, // Editor only
        };

        enum Comparison : char
        {
            Comp_Eq = '0',
            Comp_Ne = '1',
            Comp_Gt = '2',
            Comp_Ge = '3',
            Comp_Ls = '4',
            Comp_Le = '5',

            Comp_None = ' ', // Editor only
        };

        std::string mVariable;
        std::variant<std::int32_t, float> mValue = 0;
        std::uint8_t mIndex = 0;
        Function mFunction = Function_None;
        Comparison mComparison = Comp_None;

        static std::optional<DialogueCondition> load(ESMReader& esm, ESM::RefId context);

        void save(ESMWriter& esm) const;
    };
}
