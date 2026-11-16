#include "mandatoryid.hpp"

#include <utility>

#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/world/collectionbase.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>

CSMTools::MandatoryIdStage::MandatoryIdStage(const CSMWorld::CollectionBase& idCollection,
    CSMWorld::UniversalId collectionId, const std::vector<ESM::RefId>& ids)
    : mIdCollection(idCollection)
    , mCollectionId(std::move(collectionId))
    , mIds(ids)
{
}

int CSMTools::MandatoryIdStage::setup()
{
    return static_cast<int>(mIds.size());
}

void CSMTools::MandatoryIdStage::perform(int stage, CSMDoc::Messages& messages)
{
    if (mIdCollection.searchId(mIds.at(static_cast<std::size_t>(stage))) == -1
        || mIdCollection.getRecord(mIds.at(static_cast<std::size_t>(stage))).isDeleted())
        messages.add(
            mCollectionId, "Missing mandatory record: " + mIds.at(static_cast<std::size_t>(stage)).toDebugString());
}
