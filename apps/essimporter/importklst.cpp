#include "importklst.hpp"

#include <cstdint>

#include <components/esm3/esmreader.hpp>

namespace ESSImport
{

    void KLST::load(ESM::ESMReader& esm)
    {
        while (esm.isNextSub("KNAM"))
        {
            ESM::RefId refId = esm.getRefId();
            std::int32_t count = 0;
            esm.getHNT(count, "CNAM");
            mKillCounter[refId] = count;
        }

        mWerewolfKills = 0;
        esm.getHNOT(mWerewolfKills, "INTV");
        esm.getHNOT(mWerewolfKills, "VTNI"); // Not standard spec, but covers an MWSE bug
    }

}
