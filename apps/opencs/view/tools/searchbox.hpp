#pragma once

#include <cstdint>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

class QGridLayout;

namespace CSMTools
{
    class Search;
}

namespace CSVTools
{
    class SearchBox : public QWidget
    {
        Q_OBJECT

        QStackedWidget mInput;
        QLineEdit mText;
        QComboBox mRecordState;
        QCheckBox mCaseSensitive;
        QPushButton mSearch;
        QGridLayout* mLayout;
        QComboBox mMode;
        bool mSearchEnabled;
        bool mAllowReplace{ false };
        QStackedWidget mReplaceInput;
        QLineEdit mReplaceText;
        QLabel mReplacePlaceholder;
        QPushButton mReplace;

    private:
        std::int32_t mSearchResultCount = 0;

        void updateSearchButtons();

    public:
        explicit SearchBox(QWidget* parent = nullptr);

        void setEditLock(bool locked);

        void setSearchMode(bool enabled);

        void setSearchResultCount(std::int32_t resultCount);

        CSMTools::Search getSearch() const;

        std::string getReplaceText() const;

        void focus();

    private slots:

        void modeSelected(std::int32_t index);

        void textChanged(const QString& text);

        void startSearch(bool checked = true);

        void replaceAll(bool checked);

    signals:

        void startSearch(const CSMTools::Search& search);

        void replaceAll();
    };
}
