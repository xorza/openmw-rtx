#pragma once

#include <cstdint>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Weather data
    struct GAME
    {
        struct GMDT
        {
            char mCellName[64]{};
            std::int32_t mFogColour{ 0 };
            float mFogDensity{ 0.f };
            std::int32_t mCurrentWeather{ 0 }, mNextWeather{ 0 };
            std::int32_t mWeatherTransition{ 0 }; // 0-100 transition between weathers, top 3 bytes may be garbage
            float mTimeOfNextTransition{ 0.f }; // weather changes when gamehour == timeOfNextTransition
            std::int32_t mMasserPhase{ 0 }, mSecundaPhase{ 0 }; // top 3 bytes may be garbage
        };

        GMDT mGMDT;

        void load(ESM::ESMReader& esm);
    };

}
