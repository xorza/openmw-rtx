#pragma once

#include <cstdint>
#include <string>

#include "character.hpp"

namespace MWState
{
    class QuickSaveManager
    {
        std::string mSaveName;
        std::uint32_t mMaxSaves;
        std::uint32_t mSlotsVisited;
        const Slot* mOldestSlotVisited;

    private:
        bool shouldCreateNewSlot() const;
        bool isOldestSave(const Slot* compare) const;

    public:
        QuickSaveManager(std::string& saveName, std::uint32_t maxSaves);
        ///< A utility class to manage multiple quicksave slots
        ///
        /// \param saveName The name of the save ("QuickSave", "AutoSave", etc)
        /// \param maxSaves The maximum number of save slots to create before recycling old ones

        void visitSave(const Slot* saveSlot);
        ///< Visits the given \a slot \a

        const Slot* getNextQuickSaveSlot();
        ///< Get the slot that the next quicksave should use.
        ///
        ///\return Either the oldest quicksave slot visited, or nullptr if a new slot can be made
    };
}
