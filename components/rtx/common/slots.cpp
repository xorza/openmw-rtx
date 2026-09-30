#include "slots.hpp"

namespace Rtx
{
    void SlotSet::compact()
    {
        if (!mStale)
            return;

        std::erase_if(mSlots, [this](const Index slot) { return mFlags[slot] == 0; });
        mStale = false;
    }

    void SlotSet::clear()
    {
        for (const Index slot : mSlots)
            mFlags[slot] = 0;

        mSlots.clear();
        mStale = false;
    }

    void SlotChanges::note(const Index slot, const SlotNews what)
    {
        const bool arriving = what == SlotNews::Arrived;
        SlotSet& taking = arriving ? mArrived : mFreed;
        SlotSet& giving = arriving ? mFreed : mArrived;

        // Compacted here rather than left for the reader, which holds the table const and reads the
        // lists at once. A pass is paid only where the slot stood in the other set — one that
        // arrived and went, or went and came back, inside one hand-over — which a crossing does a
        // handful of times and a frame standing still never; `compact` does nothing otherwise.
        giving.remove(slot);
        giving.compact();

        taking.add(slot);
    }
}
