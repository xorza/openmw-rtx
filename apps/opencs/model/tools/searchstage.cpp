#include "searchstage.hpp"

#include <apps/opencs/model/world/idtablebase.hpp>

#include "search.hpp"
#include "searchoperation.hpp"

namespace CSMDoc
{
    class Messages;
}

CSMTools::SearchStage::SearchStage(const CSMWorld::IdTableBase* model)
    : mModel(model)
    , mOperation(nullptr)
{
}

int CSMTools::SearchStage::setup()
{
    if (mOperation)
        mSearch = mOperation->getSearch();

    mSearch.configure(mModel);

    return mModel->rowCount();
}

void CSMTools::SearchStage::perform(int stage, CSMDoc::Messages& messages)
{
    mSearch.searchRow(mModel, stage, messages);
}

void CSMTools::SearchStage::setOperation(const SearchOperation* operation)
{
    mOperation = operation;
}
