#include "slots.hpp"

namespace Rtx
{
    void SlotSet::compact()
    {
        if (mRemoved == 0)
            return;

        std::erase_if(mSlots, [this](const Index slot) {
            if (mStates[slot] != State::Removed)
                return false;

            mStates[slot] = State::Absent;
            return true;
        });
        mRemoved = 0;
    }

    void SlotSet::clear()
    {
        for (const Index slot : mSlots)
            mStates[slot] = State::Absent;

        mSlots.clear();
        mRemoved = 0;
    }

    void SlotChanges::note(const Index slot, const SlotNews what)
    {
        const bool arriving = what == SlotNews::Arrived;
        SlotSet& taking = arriving ? mArrived : mFreed;
        SlotSet& giving = arriving ? mFreed : mArrived;

        giving.remove(slot);
        taking.add(slot);
    }

    void SlotChanges::compact()
    {
        mArrived.compact();
        mFreed.compact();
    }
}
