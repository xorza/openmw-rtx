#pragma once

#include <string>

#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/doc/subview.hpp>

namespace CSMDoc
{
    class Document;
}

namespace CSVWorld
{
    class RegionMap;

    class RegionMapSubView : public CSVDoc::SubView
    {
        Q_OBJECT

        RegionMap* mRegionMap;

    public:
        RegionMapSubView(const CSMWorld::UniversalId& universalId, CSMDoc::Document& document);

        void setEditLock(bool locked) override;

    private slots:

        void editRequest(const CSMWorld::UniversalId& id, const std::string& hint);
    };
}
