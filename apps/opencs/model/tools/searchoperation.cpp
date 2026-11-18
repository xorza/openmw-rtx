#include "searchoperation.hpp"

#include <vector>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/doc/operation.hpp>
#include <apps/opencs/model/doc/state.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idtablebase.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include "search.hpp"
#include "searchstage.hpp"

CSMTools::SearchOperation::SearchOperation(CSMDoc::Document& document)
    : CSMDoc::Operation(CSMDoc::State_Searching)
{
    std::vector<CSMWorld::UniversalId::Type> types = CSMWorld::UniversalId::listTypes(
        CSMWorld::UniversalId::Class_RecordList | CSMWorld::UniversalId::Class_ResourceList);

    for (auto type : types)
        appendStage(new SearchStage(&dynamic_cast<CSMWorld::IdTableBase&>(*document.getData().getTableModel(type))));

    setDefaultSeverity(CSMDoc::Message::Severity_Info);
}

void CSMTools::SearchOperation::configure(const Search& search)
{
    mSearch = search;
}

void CSMTools::SearchOperation::appendStage(SearchStage* stage)
{
    CSMDoc::Operation::appendStage(stage);
    stage->setOperation(this);
}

const CSMTools::Search& CSMTools::SearchOperation::getSearch() const
{
    return mSearch;
}
