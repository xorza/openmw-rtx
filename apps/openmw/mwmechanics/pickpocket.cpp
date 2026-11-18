#include "pickpocket.hpp"

#include <cstdint>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/misc/rng.hpp>

#include "npcstats.hpp"

namespace MWMechanics
{

    Pickpocket::Pickpocket(const MWWorld::Ptr& thief, const MWWorld::Ptr& victim)
        : mThief(thief)
        , mVictim(victim)
    {
    }

    float Pickpocket::getChanceModifier(const MWWorld::Ptr& ptr, float add)
    {
        NpcStats& stats = ptr.getClass().getNpcStats(ptr);
        float agility = stats.getAttribute(ESM::Attribute::Agility).getModified();
        float luck = stats.getAttribute(ESM::Attribute::Luck).getModified();
        auto sneak = ptr.getClass().getSkill(ptr, ESM::Skill::Sneak);
        return (add + 0.2f * agility + 0.1f * luck + sneak) * stats.getFatigueTerm();
    }

    bool Pickpocket::getDetected(float valueTerm)
    {
        float x = getChanceModifier(mThief);
        float y = getChanceModifier(mVictim, valueTerm);

        float t = 2 * x - y;

        auto pcSneak = mThief.getClass().getSkill(mThief, ESM::Skill::Sneak);
        std::int32_t iPickMinChance = MWBase::Environment::get()
                                          .getESMStore()
                                          ->get<ESM::GameSetting>()
                                          .find("iPickMinChance")
                                          ->mValue.getInteger();
        std::int32_t iPickMaxChance = MWBase::Environment::get()
                                          .getESMStore()
                                          ->get<ESM::GameSetting>()
                                          .find("iPickMaxChance")
                                          ->mValue.getInteger();

        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        std::int32_t roll = Misc::Rng::roll0to99(prng);
        if (t < pcSneak / static_cast<float>(iPickMinChance))
        {
            return (roll > static_cast<std::int32_t>(pcSneak / static_cast<float>(iPickMinChance)));
        }
        else
        {
            t = std::min(static_cast<float>(iPickMaxChance), t);
            return (roll > static_cast<std::int32_t>(t));
        }
    }

    bool Pickpocket::pick(const MWWorld::Ptr& item, std::int32_t count)
    {
        float stackValue = static_cast<float>(item.getClass().getValue(item) * count);
        float fPickPocketMod = MWBase::Environment::get()
                                   .getESMStore()
                                   ->get<ESM::GameSetting>()
                                   .find("fPickPocketMod")
                                   ->mValue.getFloat();
        float valueTerm = 10 * fPickPocketMod * stackValue;

        return getDetected(valueTerm);
    }

    bool Pickpocket::finish()
    {
        return getDetected(0.f);
    }

}
