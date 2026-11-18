
#include "commandmacro.hpp"

#include <utility>

#include <QUndoCommand>
#include <QUndoStack>

CSMWorld::CommandMacro::CommandMacro(QUndoStack& undoStack, QString description)
    : mUndoStack(undoStack)
    , mDescription(std::move(description))
    , mStarted(false)
{
}

CSMWorld::CommandMacro::~CommandMacro()
{
    if (mStarted)
        mUndoStack.endMacro();
}

void CSMWorld::CommandMacro::push(QUndoCommand* command)
{
    if (!mStarted)
    {
        mUndoStack.beginMacro(mDescription.isEmpty() ? command->text() : mDescription);
        mStarted = true;
    }

    mUndoStack.push(command);
}
