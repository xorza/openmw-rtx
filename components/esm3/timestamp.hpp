#pragma once

#include <cstdint>

#include <components/esm/esmcommon.hpp>

namespace ESM
{
    class ESMReader;

    struct TimeStamp
    {
        float mHour;
        std::int32_t mDay;

        void load(ESMReader& esm, NAME name = "TIME");
    };
}
