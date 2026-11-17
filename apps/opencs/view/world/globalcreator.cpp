#include "globalcreator.hpp"

#include <cstdint>

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <components/esm3/variant.hpp>

#include "genericcreator.hpp"

class QUndoStack;

namespace CSVWorld
{
    void GlobalCreator::configureCreateCommand(CSMWorld::CreateCommand& command) const
    {
        auto* table = static_cast<CSMWorld::IdTable*>(getData().getTableModel(getCollectionId()));

        std::int32_t index = table->findColumnIndex(CSMWorld::Columns::ColumnId_ValueType);
        auto type = static_cast<std::int32_t>(ESM::VT_Float);

        command.addValue(index, type);
    }

    GlobalCreator::GlobalCreator(CSMWorld::Data& worldData, QUndoStack& undoStack, const CSMWorld::UniversalId& id)
        : GenericCreator(worldData, undoStack, id, true)
    {
    }
}
