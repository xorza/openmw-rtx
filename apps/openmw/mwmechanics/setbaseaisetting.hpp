#pragma once

#include <cstdint>
#include <string>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include "aisetting.hpp"

namespace MWMechanics
{
    template <class T>
    void setBaseAISetting(const ESM::RefId& id, MWMechanics::AiSetting setting, std::uint8_t value)
    {
        T copy = *MWBase::Environment::get().getESMStore()->get<T>().find(id);
        switch (setting)
        {
            case MWMechanics::AiSetting::Hello:
                copy.mAiData.mHello = value;
                break;
            case MWMechanics::AiSetting::Fight:
                copy.mAiData.mFight = value;
                break;
            case MWMechanics::AiSetting::Flee:
                copy.mAiData.mFlee = value;
                break;
            case MWMechanics::AiSetting::Alarm:
                copy.mAiData.mAlarm = value;
                break;
            default:
                assert(false);
        }
        MWBase::Environment::get().getESMStore()->overrideRecord(copy);
    }
}
