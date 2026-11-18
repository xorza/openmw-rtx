#include "stolenitems.hpp"

#include <cstdint>

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{

    void StolenItems::write(ESMWriter& esm) const
    {
        for (const auto& stolenItem : mStolenItems)
        {
            esm.writeHNRefId("NAME", stolenItem.first);
            for (const auto& [owner, count] : stolenItem.second)
            {
                if (owner.second)
                    esm.writeHNRefId("FNAM", owner.first);
                else
                    esm.writeHNRefId("ONAM", owner.first);
                esm.writeHNT("COUN", count);
            }
        }
    }

    void StolenItems::load(ESMReader& esm)
    {
        while (esm.isNextSub("NAME"))
        {
            ESM::RefId itemid = esm.getRefId();

            std::map<std::pair<ESM::RefId, bool>, std::int32_t> ownerMap;
            while (esm.isNextSub("FNAM") || esm.isNextSub("ONAM"))
            {
                const bool isFaction = (esm.retSubName() == "FNAM");
                ESM::RefId owner = esm.getRefId();
                std::int32_t count = 0;
                esm.getHNT(count, "COUN");
                ownerMap.emplace(std::make_pair(owner, isFaction), count);
            }

            mStolenItems.insert_or_assign(itemid, std::move(ownerMap));
        }
    }

}
