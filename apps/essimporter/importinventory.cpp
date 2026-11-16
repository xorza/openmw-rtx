#include "importinventory.hpp"

#include <utility>

#include <components/esm3/esmreader.hpp>

namespace ESSImport
{

    void Inventory::load(ESM::ESMReader& esm)
    {
        while (esm.isNextSub("NPCO"))
        {
            ContItem contItem{};
            esm.getHT(contItem.mCount, contItem.mItem.mData);

            InventoryItem item;
            item.mId = contItem.mItem.toString();
            item.mCount = contItem.mCount;
            item.mRelativeEquipmentSlot = -1;
            item.mLockLevel = 0;
            item.mChargeIntRemainder = 0;

            auto itemCount = static_cast<unsigned int>(std::abs(item.mCount));
            bool separateStacks = false;
            for (unsigned int i = 0; i < itemCount; ++i)
            {
                bool newStack = esm.isNextSub("XIDX");
                if (newStack)
                {
                    uint32_t idx = 0;
                    esm.getHT(idx);
                    separateStacks = true;
                    item.mCount = 1;
                }

                item.mSCRI.load(esm);

                // for XSOL and XCHG seen so far, but probably others too
                bool isDeleted = false;
                item.ESM::CellRef::loadData(esm, isDeleted);

                int32_t charge = -1;
                esm.getHNOT(charge, "XHLT");
                item.mChargeInt = charge;

                if (newStack)
                    mItems.push_back(item);
            }

            if (!separateStacks)
                mItems.push_back(std::move(item));
        }

        // equipped items
        while (esm.isNextSub("WIDX"))
        {
            // note: same item can be equipped 2 items (e.g. 2 rings)
            // and will be *stacked* in the NPCO list, unlike openmw!
            // this is currently not handled properly.

            esm.getSubHeader();
            int32_t itemIndex = 0; // index of the item in the NPCO list
            esm.getT(itemIndex);

            if (itemIndex < 0 || std::cmp_greater_equal(itemIndex, mItems.size()))
                esm.fail("equipment item index out of range");

            // appears to be a relative index for only the *possible* slots this item can be equipped in,
            // i.e. 0 most of the time
            int32_t slotIndex = 0;
            esm.getT(slotIndex);

            mItems[static_cast<std::size_t>(itemIndex)].mRelativeEquipmentSlot = slotIndex;
        }
    }

}
