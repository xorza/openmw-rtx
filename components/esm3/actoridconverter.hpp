#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "refnum.hpp"

namespace ESM
{
    class ActorIdConverter
    {
        std::vector<std::pair<ESM::RefNum&, std::int32_t>> mToConvert;

    public:
        std::map<std::int32_t, ESM::RefNum> mMappings;
        std::vector<std::int32_t> mGraveyard;

        void apply();

        void convert(ESM::RefNum& refNum, std::int32_t actorId);
    };
}
