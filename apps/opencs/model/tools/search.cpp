#include "search.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QModelIndex>
#include <QRegularExpression>
#include <QString>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/idtablebase.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/model/world/variantconversion.hpp>

void CSMTools::Search::searchTextCell(const CSMWorld::IdTableBase* model, const QModelIndex& index,
    const CSMWorld::UniversalId& id, bool writable, CSMDoc::Messages& messages) const
{
    // using QString here for easier handling of case folding.

    QString search = QString::fromUtf8(mText.c_str());
    QString text = model->data(index).toString();

    std::int32_t pos = 0;

    Qt::CaseSensitivity caseSensitivity = mCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    while ((pos = static_cast<std::int32_t>(text.indexOf(search, pos, caseSensitivity))) != -1)
    {
        std::ostringstream hint;
        hint << (writable ? 'R' : 'r') << ": " << model->getColumnId(index.column()) << " " << pos << " "
             << search.length();

        messages.add(
            id, formatDescription(text, pos, static_cast<std::int32_t>(search.length())).toUtf8().data(), hint.str());

        pos += static_cast<std::int32_t>(search.length());
    }
}

void CSMTools::Search::searchRegExCell(const CSMWorld::IdTableBase* model, const QModelIndex& index,
    const CSMWorld::UniversalId& id, bool writable, CSMDoc::Messages& messages) const
{
    // TODO: verify regular expression before starting a search
    if (!mRegExp.isValid())
        return;

    QString text = model->data(index).toString();

    QRegularExpressionMatchIterator i = mRegExp.globalMatch(text);
    while (i.hasNext())
    {
        QRegularExpressionMatch match = i.next();

        auto pos = static_cast<std::int32_t>(match.capturedStart());
        auto length = static_cast<std::int32_t>(match.capturedLength());

        std::ostringstream hint;
        hint << (writable ? 'R' : 'r') << ": " << model->getColumnId(index.column()) << " " << pos << " " << length;

        messages.add(id, formatDescription(text, pos, length).toUtf8().data(), hint.str());
    }
}

void CSMTools::Search::searchRecordStateCell(const CSMWorld::IdTableBase* model, const QModelIndex& index,
    const CSMWorld::UniversalId& id, bool writable, CSMDoc::Messages& messages) const
{
    if (writable)
        throw std::logic_error("Record state can not be modified by search and replace");

    std::int32_t data = model->data(index).toInt();

    if (data == mValue)
    {
        std::vector<std::pair<std::int32_t, std::string>> states
            = CSMWorld::Columns::getEnums(CSMWorld::Columns::ColumnId_Modification);

        const std::string hint = "r: " + std::to_string(model->getColumnId(index.column()));
        messages.add(id, states.at(static_cast<std::size_t>(data)).second, hint);
    }
}

QString CSMTools::Search::formatDescription(const QString& description, std::int32_t pos, std::int32_t length) const
{
    QString text(description);

    // split
    QString highlight = flatten(text.mid(pos, length));
    QString before = flatten(mPaddingBefore >= pos ? text.mid(0, pos) : text.mid(pos - mPaddingBefore, mPaddingBefore));
    QString after = flatten(text.mid(pos + length, mPaddingAfter));

    // compensate for Windows nonsense
    text.remove('\r');

    // join
    text = before + "<b>" + highlight + "</b>" + after;

    // improve layout for single line display
    text.replace("\n", "&lt;CR>");
    text.replace('\t', ' ');

    return text;
}

QString CSMTools::Search::flatten(const QString& text) const
{
    QString flat(text);

    flat.replace("&", "&amp;");
    flat.replace("<", "&lt;");

    return flat;
}

CSMTools::Search::Search()
    : mType(Type_None)
    , mValue(0)
    , mCase(false)
    , mIdColumn(0)
    , mTypeColumn(0)
    , mPaddingBefore(10)
    , mPaddingAfter(10)
{
}

CSMTools::Search::Search(Type type, bool caseSensitive, std::string value)
    : mType(type)
    , mText(std::move(value))
    , mValue(0)
    , mCase(caseSensitive)
    , mIdColumn(0)
    , mTypeColumn(0)
    , mPaddingBefore(10)
    , mPaddingAfter(10)
{
    if (type != Type_Text && type != Type_Id)
        throw std::logic_error("Invalid search parameter (string)");
}

CSMTools::Search::Search(Type type, bool caseSensitive, QRegularExpression value)
    : mType(type)
    , mRegExp(std::move(value))
    , mValue(0)
    , mCase(caseSensitive)
    , mIdColumn(0)
    , mTypeColumn(0)
    , mPaddingBefore(10)
    , mPaddingAfter(10)
{
    mRegExp.setPatternOptions(mCase ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
    if (type != Type_TextRegEx && type != Type_IdRegEx)
        throw std::logic_error("Invalid search parameter (RegExp)");
}

CSMTools::Search::Search(Type type, bool caseSensitive, std::int32_t value)
    : mType(type)
    , mValue(value)
    , mCase(caseSensitive)
    , mIdColumn(0)
    , mTypeColumn(0)
    , mPaddingBefore(10)
    , mPaddingAfter(10)
{
    if (type != Type_RecordState)
        throw std::logic_error("invalid search parameter (int)");
}

void CSMTools::Search::configure(const CSMWorld::IdTableBase* model)
{
    mColumns.clear();

    std::int32_t columns = model->columnCount();

    for (std::int32_t i = 0; i < columns; ++i)
    {
        CSMWorld::ColumnBase::Display display = model->getColumnDisplay(i);

        bool consider = false;

        switch (mType)
        {
            case Type_Text:
            case Type_TextRegEx:

                if (CSMWorld::ColumnBase::isText(display) || CSMWorld::ColumnBase::isScript(display))
                {
                    consider = true;
                }

                break;

            case Type_Id:
            case Type_IdRegEx:

                if (CSMWorld::ColumnBase::isId(display) || CSMWorld::ColumnBase::isScript(display))
                {
                    consider = true;
                }

                break;

            case Type_RecordState:

                if (display == CSMWorld::ColumnBase::Display_RecordState)
                    consider = true;

                break;

            case Type_None:

                break;
        }

        if (consider)
            mColumns.insert(i);
    }

    mIdColumn = model->findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    mTypeColumn = model->findColumnIndex(CSMWorld::Columns::ColumnId_RecordType);
}

void CSMTools::Search::searchRow(const CSMWorld::IdTableBase* model, std::int32_t row, CSMDoc::Messages& messages) const
{
    for (std::int32_t mColumn : mColumns)
    {
        QModelIndex index = model->index(row, mColumn);

        CSMWorld::UniversalId::Type type
            = static_cast<CSMWorld::UniversalId::Type>(model->getData(row, mTypeColumn).toInt());

        CSMWorld::UniversalId id(type, CSMWorld::getString(model->getData(row, mIdColumn)));

        bool writable = model->flags(index) & Qt::ItemIsEditable;

        switch (mType)
        {
            case Type_Text:
            case Type_Id:

                searchTextCell(model, index, id, writable, messages);
                break;

            case Type_TextRegEx:
            case Type_IdRegEx:

                searchRegExCell(model, index, id, writable, messages);
                break;

            case Type_RecordState:

                searchRecordStateCell(model, index, id, writable, messages);
                break;

            case Type_None:

                break;
        }
    }
}

void CSMTools::Search::setPadding(std::int32_t before, std::int32_t after)
{
    mPaddingBefore = before;
    mPaddingAfter = after;
}

void CSMTools::Search::replace(CSMDoc::Document& document, CSMWorld::IdTableBase* model,
    const CSMWorld::UniversalId& id, const std::string& messageHint, const std::string& replaceText) const
{
    std::istringstream stream(messageHint.c_str());

    char hint = 0, ignore = 0;
    std::int32_t columnId = 0, pos = 0, length = 0;

    if (stream >> hint >> ignore >> columnId >> pos >> length)
    {
        std::int32_t column = model->findColumnIndex(static_cast<CSMWorld::Columns::ColumnId>(columnId));

        QModelIndex index = model->getModelIndex(id.getId(), column);

        std::string text = CSMWorld::getString(model->data(index));

        std::string before = text.substr(0, static_cast<std::size_t>(pos));
        std::string after = text.substr(static_cast<std::size_t>(pos) + static_cast<std::size_t>(length));

        std::string newText = before + replaceText + after;

        document.getUndoStack().push(new CSMWorld::ModifyCommand(*model, index, QString::fromUtf8(newText.c_str())));
    }
}

bool CSMTools::Search::verify(CSMDoc::Document& document, CSMWorld::IdTableBase* model, const CSMWorld::UniversalId& id,
    const std::string& messageHint) const
{
    CSMDoc::Messages messages(CSMDoc::Message::Severity_Info);

    std::int32_t row = model->getModelIndex(id.getId(), model->findColumnIndex(CSMWorld::Columns::ColumnId_Id)).row();

    searchRow(model, row, messages);

    for (const auto& message : messages)
        if (message.mHint == messageHint)
            return true;

    return false;
}
