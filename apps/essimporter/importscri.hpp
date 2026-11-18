#pragma once

#include <cstdint>
#include <vector>

#include <components/esm3/variant.hpp>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Local variable assignments for a running script
    struct SCRI
    {
        std::string mScript;

        std::vector<std::int16_t> mShorts;
        std::vector<std::int32_t> mLongs;
        std::vector<float> mFloats;

        void load(ESM::ESMReader& esm);
    };

}
