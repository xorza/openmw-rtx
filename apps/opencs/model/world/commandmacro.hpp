#pragma once

class QUndoStack;
class QUndoCommand;

#include <QString>

namespace CSMWorld
{
    class CommandMacro
    {
        QUndoStack& mUndoStack;
        QString mDescription;
        bool mStarted;

        /// not implemented

        /// not implemented

    public:
        CommandMacro(const CommandMacro&) = delete;
        CommandMacro& operator=(const CommandMacro&) = delete;
        /// If \a description is empty, the description of the first command is used.
        explicit CommandMacro(QUndoStack& undoStack, QString description = "");

        ~CommandMacro();

        void push(QUndoCommand* command);
    };
}
