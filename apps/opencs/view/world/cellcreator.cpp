#include "cellcreator.hpp"

#include <cstdint>
#include <limits>
#include <sstream>

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idtree.hpp>

#include "genericcreator.hpp"

class QUndoStack;

std::string CSVWorld::CellCreator::getId() const
{
    if (mType->currentIndex() == 0)
        return GenericCreator::getId();

    std::ostringstream stream;

    stream << "#" << mX->value() << " " << mY->value();

    return stream.str();
}

void CSVWorld::CellCreator::configureCreateCommand(CSMWorld::CreateCommand& command) const
{
    CSMWorld::IdTree* model = &getData().getTableModel<CSMWorld::IdTree>(getCollectionId());

    std::int32_t parentIndex = model->findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
    std::int32_t index = model->findNestedColumnIndex(parentIndex, CSMWorld::Columns::ColumnId_Interior);
    command.addNestedValue(parentIndex, index, mType->currentIndex() == 0);
}

CSVWorld::CellCreator::CellCreator(CSMWorld::Data& worldData, QUndoStack& undoStack, const CSMWorld::UniversalId& id)
    : GenericCreator(worldData, undoStack, id)
{
    mY = new QSpinBox(this);
    mY->setVisible(false);
    mY->setMinimum(std::numeric_limits<std::int32_t>::min());
    mY->setMaximum(std::numeric_limits<std::int32_t>::max());
    connect(mY, qOverload<std::int32_t>(&QSpinBox::valueChanged), this, &CellCreator::valueChanged);
    insertAtBeginning(mY, true);

    mYLabel = new QLabel("Y", this);
    mYLabel->setVisible(false);
    insertAtBeginning(mYLabel, false);

    mX = new QSpinBox(this);
    mX->setVisible(false);
    mX->setMinimum(std::numeric_limits<std::int32_t>::min());
    mX->setMaximum(std::numeric_limits<std::int32_t>::max());
    connect(mX, qOverload<std::int32_t>(&QSpinBox::valueChanged), this, &CellCreator::valueChanged);
    insertAtBeginning(mX, true);

    mXLabel = new QLabel("X", this);
    mXLabel->setVisible(false);
    insertAtBeginning(mXLabel, false);

    mType = new QComboBox(this);

    mType->addItem("Interior Cell");
    mType->addItem("Exterior Cell");

    connect(mType, qOverload<std::int32_t>(&QComboBox::currentIndexChanged), this, &CellCreator::setType);

    insertAtBeginning(mType, false);
}

void CSVWorld::CellCreator::reset()
{
    mX->setValue(0);
    mY->setValue(0);
    mType->setCurrentIndex(0);
    setType(0);
    GenericCreator::reset();
}

void CSVWorld::CellCreator::setType(std::int32_t index)
{
    setManualEditing(index == 0);
    mXLabel->setVisible(index == 1);
    mX->setVisible(index == 1);
    mYLabel->setVisible(index == 1);
    mY->setVisible(index == 1);

    // The cell name is limited to 64 characters. (ESM::Header::GMDT::mCurrentCell)
    std::string text = mType->currentText().toStdString();
    if (text == "Interior Cell")
        GenericCreator::setEditorMaxLength(64);
    else
        GenericCreator::setEditorMaxLength(32767);

    update();
}

void CSVWorld::CellCreator::valueChanged(std::int32_t index)
{
    update();
}

void CSVWorld::CellCreator::cloneMode(const std::string& originId, const CSMWorld::UniversalId::Type type)
{
    CSVWorld::GenericCreator::cloneMode(originId, type);
    if (*(originId.begin()) == '#') // if originid points to the exterior cell
    {
        setType(1); // enable x and y controls
        mType->setCurrentIndex(1);
    }
    else
    {
        setType(0);
        mType->setCurrentIndex(0);
    }
}

std::string CSVWorld::CellCreator::getErrors() const
{
    std::string errors;
    if (mType->currentIndex() == 0)
    {
        errors = GenericCreator::getErrors();
    }
    else if (getData().hasId(getId()))
    {
        errors = "The Exterior Cell is already exist";
    }
    return errors;
}
