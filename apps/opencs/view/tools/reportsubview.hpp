#pragma once

#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/doc/subview.hpp>

class QObject;

namespace CSMDoc
{
    class Document;
}

namespace CSVTools
{
    class ReportTable;

    class ReportSubView : public CSVDoc::SubView
    {
        Q_OBJECT

        ReportTable* mTable;
        CSMDoc::Document& mDocument;
        int mRefreshState;

    public:
        ReportSubView(const CSMWorld::UniversalId& id, CSMDoc::Document& document);

        void setEditLock(bool locked) override;

    private slots:

        void refreshRequest();
    };
}
