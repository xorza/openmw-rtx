#pragma once

#include <apps/opencs/model/world/universalid.hpp>

#include "creator.hpp"
#include "genericcreator.hpp"

class QUndoStack;

namespace CSMDoc
{
    class Document;
}

namespace CSMWorld
{
    class CreateCommand;
    class Data;
}

namespace CSVWorld
{
    class DialogueCreator : public GenericCreator
    {
        int mType;

    protected:
        void configureCreateCommand(CSMWorld::CreateCommand& command) const override;

    public:
        explicit DialogueCreator(
            CSMWorld::Data& worldData, QUndoStack& undoStack, const CSMWorld::UniversalId& id, int type);
    };

    class TopicCreatorFactory : public CreatorFactoryBase
    {
    public:
        Creator* makeCreator(CSMDoc::Document& document, const CSMWorld::UniversalId& id) const override;
        ///< The ownership of the returned Creator is transferred to the caller.
    };

    class JournalCreatorFactory : public CreatorFactoryBase
    {
    public:
        Creator* makeCreator(CSMDoc::Document& document, const CSMWorld::UniversalId& id) const override;
        ///< The ownership of the returned Creator is transferred to the caller.
    };
}
