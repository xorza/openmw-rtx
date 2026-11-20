#include "predictedbands.hpp"

#include <cassert>
#include <mutex>
#include <vector>

namespace Rtx
{
    PredictedBands::Slot PredictedBands::open()
    {
        const std::scoped_lock lock(mMutex);
        if (!mFree.empty())
        {
            const Slot slot = mFree.back();
            mFree.pop_back();
            mOpen[slot] = true;
            return slot;
        }
        mCentres.emplace_back();
        mOpen.push_back(true);
        return static_cast<Slot>(mCentres.size() - 1);
    }

    void PredictedBands::place(const Slot slot, const osg::Vec3f& centre)
    {
        const std::scoped_lock lock(mMutex);
        assert(slot < mCentres.size() && mOpen[slot] && "a centre placed in a slot not open");
        mCentres[slot] = centre;
    }

    void PredictedBands::clear(const Slot slot)
    {
        const std::scoped_lock lock(mMutex);
        assert(slot < mCentres.size() && mOpen[slot] && "a slot cleared that is not open");
        mCentres[slot].reset();
    }

    void PredictedBands::close(const Slot slot)
    {
        const std::scoped_lock lock(mMutex);
        assert(slot < mCentres.size() && mOpen[slot] && "a slot closed that is not open");
        mCentres[slot].reset();
        mOpen[slot] = false;
        mFree.push_back(slot);
    }

    void PredictedBands::collect(std::vector<osg::Vec3f>& into) const
    {
        const std::scoped_lock lock(mMutex);
        for (const std::optional<osg::Vec3f>& centre : mCentres)
            if (centre.has_value())
                into.push_back(*centre);
    }
}
