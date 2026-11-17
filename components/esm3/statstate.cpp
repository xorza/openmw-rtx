#include "statstate.hpp"

#include <cstdint>

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    template <typename T>
    StatState<T>::StatState()
        : mBase(0)
        , mMod(0)
        , mCurrent(0)
        , mDamage(0)
        , mProgress(0)
    {
    }

    template <typename T>
    void StatState<T>::load(ESMReader& esm, bool intFallback)
    {
        // We changed stats values from integers to floats; ensure backwards compatibility
        if (intFallback)
        {
            std::int32_t base = 0;
            esm.getHNT(base, "STBA");
            mBase = static_cast<T>(base);

            std::int32_t mod = 0;
            esm.getHNOT(mod, "STMO");
            mMod = static_cast<T>(mod);

            std::int32_t current = 0;
            esm.getHNOT(current, "STCU");
            mCurrent = static_cast<T>(current);
        }
        else
        {
            mBase = 0;
            esm.getHNT(mBase, "STBA");

            mMod = 0;
            esm.getHNOT(mMod, "STMO");

            mCurrent = 0;
            esm.getHNOT(mCurrent, "STCU");

            mDamage = 0;
            esm.getHNOT(mDamage, "STDF");

            mProgress = 0;
        }

        esm.getHNOT(mDamage, "STDF");

        mProgress = 0;
        esm.getHNOT(mProgress, "STPR");
    }

    template <typename T>
    void StatState<T>::save(ESMWriter& esm) const
    {
        esm.writeHNT("STBA", mBase);

        if (mMod != 0)
            esm.writeHNT("STMO", mMod);

        if (mCurrent != 0)
            esm.writeHNT("STCU", mCurrent);

        if (mDamage != 0)
            esm.writeHNT("STDF", mDamage);

        if (mProgress != 0)
            esm.writeHNT("STPR", mProgress);
    }

    template struct StatState<std::int32_t>;
    template struct StatState<float>;
}
