#include "extendedcommandconfigurator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <QCheckBox>
#include <QGroupBox>
#include <QLayout>
#include <QPushButton>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/commanddispatcher.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/universalid.hpp>

CSVWorld::ExtendedCommandConfigurator::ExtendedCommandConfigurator(
    CSMDoc::Document& document, const CSMWorld::UniversalId& id, QWidget* parent)
    : QWidget(parent)
    , mNumUsedCheckBoxes(0)
    , mNumChecked(0)
    , mMode(Mode_None)
    , mData(document.getData())
    , mEditLock(false)
{
    mCommandDispatcher = new CSMWorld::CommandDispatcher(document, id, this);

    connect(&mData, &CSMWorld::Data::idListChanged, this, &ExtendedCommandConfigurator::dataIdListChanged);

    mPerformButton = new QPushButton(this);
    mPerformButton->setDefault(true);
    mPerformButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(mPerformButton, &QPushButton::clicked, this, &ExtendedCommandConfigurator::performExtendedCommand);

    mCancelButton = new QPushButton("Cancel", this);
    mCancelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(mCancelButton, &QPushButton::clicked, this, &ExtendedCommandConfigurator::done);

    mTypeGroup = new QGroupBox(this);

    auto* groupLayout = new QGridLayout(mTypeGroup);
    groupLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mTypeGroup->setLayout(groupLayout);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setSizeConstraint(QLayout::SetNoConstraint);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mTypeGroup);
    mainLayout->addWidget(mPerformButton);
    mainLayout->addWidget(mCancelButton);
}

void CSVWorld::ExtendedCommandConfigurator::configure(
    CSVWorld::ExtendedCommandConfigurator::Mode mode, const std::vector<std::string>& selectedIds)
{
    mMode = mode;
    if (mMode != Mode_None)
    {
        mPerformButton->setText((mMode == Mode_Delete) ? "Extended Delete" : "Extended Revert");
        mSelectedIds = selectedIds;
        mCommandDispatcher->setSelection(mSelectedIds);

        setupCheckBoxes(mCommandDispatcher->getExtendedTypes());
        setupGroupLayout();
        lockWidgets(mEditLock);
    }
}

void CSVWorld::ExtendedCommandConfigurator::setEditLock(bool locked)
{
    if (mEditLock != locked)
    {
        mEditLock = locked;
        lockWidgets(mEditLock);
    }
}

void CSVWorld::ExtendedCommandConfigurator::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    setupGroupLayout();
}

void CSVWorld::ExtendedCommandConfigurator::setupGroupLayout()
{
    if (mMode == Mode_None)
    {
        return;
    }

    std::int32_t groupWidth = mTypeGroup->geometry().width();
    auto* layout = qobject_cast<QGridLayout*>(mTypeGroup->layout());

    // Find the optimal number of rows to place the checkboxes within the available space
    std::int32_t divider = 1;
    do
    {
        while (layout->itemAt(0) != nullptr)
        {
            layout->removeItem(layout->itemAt(0));
        }

        std::int32_t counter = 0;
        std::int32_t itemsPerRow = mNumUsedCheckBoxes / divider;
        auto current = mTypeCheckBoxes.begin();
        auto end = mTypeCheckBoxes.end();
        for (; current != end; ++current)
        {
            if (counter < mNumUsedCheckBoxes)
            {
                std::int32_t row = counter / itemsPerRow;
                std::int32_t column = counter - (counter / itemsPerRow) * itemsPerRow;
                layout->addWidget(current->first, row, column);
            }
            ++counter;
        }
        divider *= 2;
    } while (groupWidth < mTypeGroup->sizeHint().width() && divider <= mNumUsedCheckBoxes);
}

void CSVWorld::ExtendedCommandConfigurator::setupCheckBoxes(const std::vector<CSMWorld::UniversalId>& types)
{
    // Make sure that we have enough checkboxes
    auto numTypes = static_cast<std::int32_t>(types.size());
    auto numCheckBoxes = static_cast<std::int32_t>(mTypeCheckBoxes.size());
    if (numTypes > numCheckBoxes)
    {
        for (std::int32_t i = numTypes - numCheckBoxes; i > 0; --i)
        {
            auto* checkBox = new QCheckBox(mTypeGroup);
            connect(checkBox, &QCheckBox::checkStateChanged, this, &ExtendedCommandConfigurator::checkBoxStateChanged);
            mTypeCheckBoxes.insert(std::make_pair(checkBox, CSMWorld::UniversalId::Type_None));
        }
    }

    // Set up the checkboxes
    std::int32_t counter = 0;
    auto current = mTypeCheckBoxes.begin();
    auto end = mTypeCheckBoxes.end();
    for (; current != end; ++current)
    {
        if (counter < numTypes)
        {
            CSMWorld::UniversalId type = types[static_cast<std::size_t>(counter)];
            current->first->setText(QString::fromUtf8(type.getTypeName().c_str()));
            current->first->setChecked(true);
            current->second = std::move(type);
            ++counter;
        }
        else
        {
            current->first->hide();
        }
    }
    mNumChecked = mNumUsedCheckBoxes = numTypes;
}

void CSVWorld::ExtendedCommandConfigurator::lockWidgets(bool locked)
{
    mPerformButton->setEnabled(!mEditLock && mNumChecked > 0);

    auto current = mTypeCheckBoxes.begin();
    auto end = mTypeCheckBoxes.end();
    for (std::int32_t i = 0; current != end && i < mNumUsedCheckBoxes; ++current, ++i)
    {
        current->first->setEnabled(!mEditLock);
    }
}

void CSVWorld::ExtendedCommandConfigurator::performExtendedCommand()
{
    std::vector<CSMWorld::UniversalId> types;

    auto current = mTypeCheckBoxes.begin();
    auto end = mTypeCheckBoxes.end();
    for (; current != end; ++current)
    {
        if (current->first->isChecked())
        {
            types.push_back(current->second);
        }
    }

    mCommandDispatcher->setExtendedTypes(types);
    if (mMode == Mode_Delete)
    {
        mCommandDispatcher->executeExtendedDelete();
    }
    else
    {
        mCommandDispatcher->executeExtendedRevert();
    }
    emit done();
}

void CSVWorld::ExtendedCommandConfigurator::checkBoxStateChanged(std::int32_t state)
{
    switch (state)
    {
        case Qt::Unchecked:
            --mNumChecked;
            break;
        case Qt::Checked:
            ++mNumChecked;
            break;
        // Each branch states its own case, kept apart to be read and changed as its own.
        // NOLINTBEGIN(bugprone-branch-clone)
        case Qt::PartiallyChecked: // Not used
            break;
        default:
            break;
            // NOLINTEND(bugprone-branch-clone)
    }

    mPerformButton->setEnabled(mNumChecked > 0);
}

void CSVWorld::ExtendedCommandConfigurator::dataIdListChanged()
{
    bool idsRemoved = false;
    for (std::int32_t i = 0; std::cmp_less(i, mSelectedIds.size()); ++i)
    {
        if (!mData.hasId(mSelectedIds[static_cast<std::size_t>(i)]))
        {
            std::swap(mSelectedIds[static_cast<std::size_t>(i)], mSelectedIds.back());
            mSelectedIds.pop_back();
            idsRemoved = true;
            --i;
        }
    }

    // If all selected IDs were removed, cancel the configurator
    if (mSelectedIds.empty())
    {
        emit done();
        return;
    }

    if (idsRemoved)
    {
        mCommandDispatcher->setSelection(mSelectedIds);
    }
}
