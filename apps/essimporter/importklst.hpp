#pragma once

#include <cstdint>
#include <map>

#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Kill Stats
    struct KLST
    {
        void load(ESM::ESMReader& esm);

        std::map<ESM::RefId, std::int32_t> mKillCounter;

        std::int32_t mWerewolfKills{};
    };

}
