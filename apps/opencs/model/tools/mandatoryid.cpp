#include "mandatoryid.hpp"

#include <cstddef>
#include <cstdint>
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

std::int32_t CSMTools::MandatoryIdStage::setup()
{
    return static_cast<std::int32_t>(mIds.size());
}

void CSMTools::MandatoryIdStage::perform(std::int32_t stage, CSMDoc::Messages& messages)
{
    if (mIdCollection.searchId(mIds.at(static_cast<std::size_t>(stage))) == -1
        || mIdCollection.getRecord(mIds.at(static_cast<std::size_t>(stage))).isDeleted())
        messages.add(
            mCollectionId, "Missing mandatory record: " + mIds.at(static_cast<std::size_t>(stage)).toDebugString());
}
