#include "pastroom.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Rtx
{
    std::string describe(const PastRoom& past)
    {
        struct Table
        {
            std::string_view mName;
            std::uint32_t mPast;
        };
        const std::array tables{ Table{ "placements", past.mPlacements }, Table{ "meshes", past.mMeshes },
            Table{ "materials", past.mMaterials }, Table{ "emitters", past.mEmitters }, Table{ "frozen", past.mFrozen },
            Table{ "chain keys", past.mChainKeys }, Table{ "glows", past.mGlows },
            Table{ "ring models", past.mRingModels } };

        std::string grew;
        for (const Table& table : tables)
        {
            if (table.mPast == 0)
                continue;
            if (!grew.empty())
                grew += ", ";
            grew += std::format("{} {}", table.mName, table.mPast);
        }
        return grew.empty() ? std::string("no table grew past its room") : "grew past their room: " + grew;
    }
}
