#include "importscri.hpp"

#include <cmath>
#include <cstdint>

#include <components/esm3/esmreader.hpp>

namespace ESSImport
{

    void SCRI::load(ESM::ESMReader& esm)
    {
        mScript = esm.getHNOString("SCRI");

        std::int32_t numShorts = 0, numLongs = 0, numFloats = 0;
        if (esm.isNextSub("SLCS"))
        {
            esm.getSubHeader();
            esm.getT(numShorts);
            esm.getT(numLongs);
            esm.getT(numFloats);
        }

        if (esm.isNextSub("SLSD"))
        {
            esm.getSubHeader();
            for (std::int32_t i = 0; i < numShorts; ++i)
            {
                std::int16_t val = 0;
                esm.getT(val);
                mShorts.push_back(val);
            }
        }
        // I haven't seen Longs in a save file yet, but SLLD would make sense for the name
        // TODO: test this
        if (esm.isNextSub("SLLD"))
        {
            esm.getSubHeader();
            for (std::int32_t i = 0; i < numLongs; ++i)
            {
                std::int32_t val = 0;
                esm.getT(val);
                mLongs.push_back(val);
            }
        }
        if (esm.isNextSub("SLFD"))
        {
            esm.getSubHeader();
            for (std::int32_t i = 0; i < numFloats; ++i)
            {
                float val = NAN;
                esm.getT(val);
                mFloats.push_back(val);
            }
        }
    }

}
