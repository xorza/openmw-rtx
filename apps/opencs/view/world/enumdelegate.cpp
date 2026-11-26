#include "enumdelegate.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <QApplication>
#include <QComboBox>

#include <apps/opencs/model/world/commands.hpp>

#include "util.hpp"

std::int32_t CSVWorld::EnumDelegate::getValueIndex(const QModelIndex& index, std::int32_t role) const
{
    if (index.isValid() && index.data(role).isValid())
    {
        std::int32_t value = index.data(role).toInt();

        auto size = static_cast<std::int32_t>(mValues.size());
        for (std::int32_t i = 0; i < size; ++i)
        {
            if (value == mValues.at(static_cast<std::size_t>(i)).first)
            {
                return i;
            }
        }
    }
    return -1;
}

void CSVWorld::EnumDelegate::setModelDataImp(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    if (auto* comboBox = dynamic_cast<QComboBox*>(editor))
    {
        QString value = comboBox->currentText();

<<<<<<< apps/opencs/view/world/enumdelegate.cpp (ours)
        for (const auto& eachValue : mValues)
            if (eachValue.second == value)
            {
                // do nothing if the value has not changed
                if (model->data(index).toInt() != eachValue.first)
                    addCommands(model, index, eachValue.first);
=======
        for (const auto& [id, name] : mValues)
            if (name == value)
            {
                // do nothing if the value has not changed
                if (model->data(index).toInt() != id)
                    addCommands(model, index, id);
>>>>>>> apps/opencs/view/world/enumdelegate.cpp (theirs)
                break;
            }
    }
}

void CSVWorld::EnumDelegate::addCommands(QAbstractItemModel* model, const QModelIndex& index, std::int32_t type) const
{
    getUndoStack().push(new CSMWorld::ModifyCommand(*model, index, type));
}

CSVWorld::EnumDelegate::EnumDelegate(const std::vector<std::pair<std::int32_t, QString>>& values,
    CSMWorld::CommandDispatcher* dispatcher, CSMDoc::Document& document, QObject* parent)
    : CommandDelegate(dispatcher, document, parent)
    , mValues(values)
{
}

QWidget* CSVWorld::EnumDelegate::createEditor(
    QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    return createEditor(parent, option, index, CSMWorld::ColumnBase::Display_None);
}

QWidget* CSVWorld::EnumDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
    const QModelIndex& index, CSMWorld::ColumnBase::Display display) const
{
    if (!index.data(Qt::EditRole).isValid() && !index.data(Qt::DisplayRole).isValid())
        return nullptr;

    auto* comboBox = new QComboBox(parent);

<<<<<<< apps/opencs/view/world/enumdelegate.cpp (ours)
    for (const auto& value : mValues)
        comboBox->addItem(value.second);
=======
    for (const auto& [_, name] : mValues)
        comboBox->addItem(name);
>>>>>>> apps/opencs/view/world/enumdelegate.cpp (theirs)

    comboBox->setMaxVisibleItems(20);

    return comboBox;
}

void CSVWorld::EnumDelegate::setEditorData(QWidget* editor, const QModelIndex& index, bool tryDisplay) const
{
    if (auto* comboBox = dynamic_cast<QComboBox*>(editor))
    {
        std::int32_t role = Qt::EditRole;
        if (tryDisplay && !index.data(role).isValid())
        {
            role = Qt::DisplayRole;
            if (!index.data(role).isValid())
            {
                return;
            }
        }

        std::int32_t valueIndex = getValueIndex(index, role);
        if (valueIndex != -1)
        {
            comboBox->setCurrentIndex(valueIndex);
        }
    }
}

void CSVWorld::EnumDelegate::paint(
    QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    std::int32_t valueIndex = getValueIndex(index);
    if (valueIndex != -1)
    {
        QStyleOptionViewItem itemOption(option);
        itemOption.text = mValues.at(static_cast<std::size_t>(valueIndex)).second;
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter);
    }
}

QSize CSVWorld::EnumDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    std::int32_t valueIndex = getValueIndex(index);
    if (valueIndex != -1)
    {
        // Calculate the size hint as for a combobox.
        // So, the whole text is visible (isn't elided) when the editor is created
        QStyleOptionComboBox itemOption;
        itemOption.fontMetrics = option.fontMetrics;
        itemOption.palette = option.palette;
        itemOption.rect = option.rect;
        itemOption.state = option.state;

        const QString& valueText = mValues.at(static_cast<std::size_t>(valueIndex)).second;
        QSize valueSize = QSize(itemOption.fontMetrics.horizontalAdvance(valueText), itemOption.fontMetrics.height());
        itemOption.currentText = valueText;
        return QApplication::style()->sizeFromContents(QStyle::CT_ComboBox, &itemOption, valueSize);
    }
    return option.rect.size();
}

CSVWorld::EnumDelegateFactory::EnumDelegateFactory(const char** names, bool allowNone)
{
    assert(names);

    if (allowNone)
        add(-1, "");

    for (std::int32_t i = 0; names[i]; ++i)
        add(i, names[i]);
}

CSVWorld::EnumDelegateFactory::EnumDelegateFactory(
    const std::vector<std::pair<std::int32_t, std::string>>& names, bool allowNone)
{
    if (allowNone)
        add(-1, "");

    auto size = static_cast<std::int32_t>(names.size());

    for (std::int32_t i = 0; i < size; ++i)
        add(names[static_cast<std::size_t>(i)].first, names[static_cast<std::size_t>(i)].second.c_str());
}

CSVWorld::CommandDelegate* CSVWorld::EnumDelegateFactory::makeDelegate(
    CSMWorld::CommandDispatcher* dispatcher, CSMDoc::Document& document, QObject* parent) const
{
    return new EnumDelegate(mValues, dispatcher, document, parent);
}

void CSVWorld::EnumDelegateFactory::add(std::int32_t value, const QString& name)
{
    auto pair = std::make_pair(value, name);

    for (auto it = mValues.begin(); it != mValues.end(); ++it)
    {
        if (it->second > name)
        {
            mValues.insert(it, pair);
            return;
        }
    }

    mValues.emplace_back(value, name);
}
