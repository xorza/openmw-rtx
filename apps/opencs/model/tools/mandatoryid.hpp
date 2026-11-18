#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/esm/refid.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class CollectionBase;
}

namespace CSMTools
{
    /// \brief Verify stage: make sure that records with specific IDs exist.
    class MandatoryIdStage : public CSMDoc::Stage
    {
        const CSMWorld::CollectionBase& mIdCollection;
        CSMWorld::UniversalId mCollectionId;
        std::vector<ESM::RefId> mIds;

    public:
        MandatoryIdStage(const CSMWorld::CollectionBase& idCollection, CSMWorld::UniversalId collectionId,
            const std::vector<ESM::RefId>& ids);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
