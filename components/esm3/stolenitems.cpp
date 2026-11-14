#include "stolenitems.hpp"

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

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

            std::map<std::pair<ESM::RefId, bool>, int> ownerMap;
            while (esm.isNextSub("FNAM") || esm.isNextSub("ONAM"))
            {
                const bool isFaction = (esm.retSubName() == "FNAM");
                ESM::RefId owner = esm.getRefId();
                int count = 0;
                esm.getHNT(count, "COUN");
                ownerMap.emplace(std::make_pair(owner, isFaction), count);
            }

            mStolenItems.insert_or_assign(itemid, std::move(ownerMap));
        }
    }

}
