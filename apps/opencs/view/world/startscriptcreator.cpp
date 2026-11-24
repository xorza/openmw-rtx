#include "startscriptcreator.hpp"

#include <cstdint>
#include <memory>

#include <QLabel>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/idcompletionmanager.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/view/widget/droplineedit.hpp>

#include "genericcreator.hpp"

class QUndoStack;

std::string CSVWorld::StartScriptCreator::getId() const
{
    return mScript->text().toUtf8().constData();
}

CSMWorld::IdTable& CSVWorld::StartScriptCreator::getStartScriptsTable() const
{
    return getData().getTableModel<CSMWorld::IdTable>(getCollectionId());
}

CSVWorld::StartScriptCreator::StartScriptCreator(CSMWorld::Data& worldData, QUndoStack& undoStack,
    const CSMWorld::UniversalId& id, CSMWorld::IdCompletionManager& completionManager)
    : GenericCreator(worldData, undoStack, id)
{
    setManualEditing(false);

    // Add script ID input label.
    auto* label = new QLabel("Script", this);
    insertBeforeButtons(label, false);

    // Add script ID input with auto-completion.
    // Only existing script IDs are accepted so no ID validation is performed.
    CSMWorld::ColumnBase::Display displayType = CSMWorld::ColumnBase::Display_Script;
    mScript = new CSVWidget::DropLineEdit(displayType, this);
    mScript->setCompleter(completionManager.getCompleter(displayType).get());
    insertBeforeButtons(mScript, true);

    connect(mScript, &CSVWidget::DropLineEdit::textChanged, this, &StartScriptCreator::scriptChanged);
    connect(mScript, &CSVWidget::DropLineEdit::returnPressed, this, &StartScriptCreator::inputReturnPressed);
}

void CSVWorld::StartScriptCreator::cloneMode(const std::string& originId, const CSMWorld::UniversalId::Type type)
{
    CSVWorld::GenericCreator::cloneMode(originId, type);

    // Look up cloned record in start scripts table and set script ID text.
    CSMWorld::IdTable& table = getStartScriptsTable();
    std::int32_t column = table.findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    mScript->setText(table.data(table.getModelIndex(originId, column)).toString());
}

std::string CSVWorld::StartScriptCreator::getErrors() const
{
    const ESM::RefId scriptId = ESM::RefId::stringRefId(getId());

    // Check user input for any errors.
    std::string errors;
    if (scriptId.empty())
    {
        errors = "No Script ID entered";
    }
    else if (getData().getScripts().searchId(scriptId) == -1)
    {
        errors = "Script ID not found";
    }
    else if (getData().getStartScripts().searchId(scriptId) > -1)
    {
        errors = "Script with this ID already registered as Start Script";
    }

    return errors;
}

void CSVWorld::StartScriptCreator::focus()
{
    mScript->setFocus();
}

void CSVWorld::StartScriptCreator::reset()
{
    CSVWorld::GenericCreator::reset();
    mScript->setText("");
}

void CSVWorld::StartScriptCreator::scriptChanged()
{
    update();
}

CSVWorld::Creator* CSVWorld::StartScriptCreatorFactory::makeCreator(
    CSMDoc::Document& document, const CSMWorld::UniversalId& id) const
{
    return new StartScriptCreator(document.getData(), document.getUndoStack(), id, document.getIdCompletionManager());
}
