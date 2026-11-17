#pragma once

#include <cstdint>

#include <apps/opencs/model/tools/search.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/doc/subview.hpp>

#include "searchbox.hpp"

namespace CSMDoc
{
    class Document;
}

namespace CSVWorld
{
    class TableBottomBox;
}

namespace CSVTools
{
    class ReportTable;

    class SearchSubView : public CSVDoc::SubView
    {
        Q_OBJECT

        ReportTable* mTable;
        SearchBox mSearchBox;
        CSMDoc::Document& mDocument;
        CSMTools::Search mSearch;
        bool mAllowReplace{ false };
        CSVWorld::TableBottomBox* mBottom;

    private:
        void replace(bool selection);

    protected:
        void showEvent(QShowEvent* event) override;

    public:
        SearchSubView(const CSMWorld::UniversalId& id, CSMDoc::Document& document);

        void setEditLock(bool locked) override;

        void setStatusBar(bool show) override;

    private slots:

        void stateChanged(std::int32_t state, CSMDoc::Document* document);

        void startSearch(const CSMTools::Search& search);

        void replaceRequest();

        void replaceAllRequest();

        void tableSizeUpdate();

        void operationDone(std::int32_t type, bool failed);
    };
}
