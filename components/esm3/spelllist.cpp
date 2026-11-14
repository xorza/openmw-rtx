#include "spelllist.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

#include <components/misc/strings/algorithm.hpp>

namespace ESM
{

    void SpellList::add(ESMReader& esm)
    {
        mList.push_back(esm.getRefId());
    }

    void SpellList::save(ESMWriter& esm) const
    {
        for (auto entry : mList)
        {
            esm.writeHNRefId("NPCS", entry, 32);
        }
    }

    bool SpellList::exists(const ESM::RefId& spell) const
    {
        for (auto entry : mList)
            if (entry == spell)
                return true;
        return false;
    }

}
