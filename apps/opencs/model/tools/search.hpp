#pragma once

#include <cstdint>
#include <set>
#include <string>

#include <QMetaType>
#include <QRegularExpression>

class QModelIndex;

namespace CSMDoc
{
    class Messages;
    class Document;
}

namespace CSMWorld
{
    class IdTableBase;
    class UniversalId;
}

namespace CSMTools
{
    class Search
    {
    public:
        enum Type
        {
            Type_Text = 0,
            Type_TextRegEx = 1,
            Type_Id = 2,
            Type_IdRegEx = 3,
            Type_RecordState = 4,
            Type_None = 5
        };

    private:
        Type mType;
        std::string mText;
        QRegularExpression mRegExp;
        std::int32_t mValue;
        bool mCase;
        std::set<std::int32_t> mColumns;
        std::int32_t mIdColumn;
        std::int32_t mTypeColumn;
        std::int32_t mPaddingBefore;
        std::int32_t mPaddingAfter;

        void searchTextCell(const CSMWorld::IdTableBase* model, const QModelIndex& index,
            const CSMWorld::UniversalId& id, bool writable, CSMDoc::Messages& messages) const;

        void searchRegExCell(const CSMWorld::IdTableBase* model, const QModelIndex& index,
            const CSMWorld::UniversalId& id, bool writable, CSMDoc::Messages& messages) const;

        void searchRecordStateCell(const CSMWorld::IdTableBase* model, const QModelIndex& index,
            const CSMWorld::UniversalId& id, bool writable, CSMDoc::Messages& messages) const;

        QString formatDescription(const QString& description, std::int32_t pos, std::int32_t length) const;

        QString flatten(const QString& text) const;

    public:
        Search();

        Search(Type type, bool caseSensitive, std::string value);

        Search(Type type, bool caseSensitive, QRegularExpression value);

        Search(Type type, bool caseSensitive, std::int32_t value);

        // Configure search for the specified model.
        void configure(const CSMWorld::IdTableBase* model);

        // Search row in \a model and store results in \a messages.
        //
        // \attention *this needs to be configured for \a model.
        void searchRow(const CSMWorld::IdTableBase* model, std::int32_t row, CSMDoc::Messages& messages) const;

        void setPadding(std::int32_t before, std::int32_t after);

        // Configuring *this for the model is not necessary when calling this function.
        void replace(CSMDoc::Document& document, CSMWorld::IdTableBase* model, const CSMWorld::UniversalId& id,
            const std::string& messageHint, const std::string& replaceText) const;

        // Check if model still matches search results.
        bool verify(CSMDoc::Document& document, CSMWorld::IdTableBase* model, const CSMWorld::UniversalId& id,
            const std::string& messageHint) const;
    };
}

Q_DECLARE_METATYPE(CSMTools::Search)
