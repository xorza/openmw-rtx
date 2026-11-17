#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    struct RegionWeatherState
    {
        RefId mWeather;
        std::map<RefId, std::uint8_t> mChances;
    };

    struct WeatherState
    {
        ESM::RefId mCurrentRegion;
        float mTimePassed{};
        bool mFastForward{};
        float mWeatherUpdateTime{};
        float mTransitionFactor{};
        RefId mCurrentWeather;
        RefId mNextWeather;
        RefId mQueuedWeather;
        std::map<ESM::RefId, RegionWeatherState> mRegions;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}
