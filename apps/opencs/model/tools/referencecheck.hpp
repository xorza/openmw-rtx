#pragma once

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/refcollection.hpp>

namespace ESM
{
    struct BodyPart;
    struct Faction;
}

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class RefIdCollection;
    class RefIdData;
    struct Cell;
}

namespace CSMTools
{
    class ReferenceCheckStage : public CSMDoc::Stage
    {
    public:
        ReferenceCheckStage(const CSMWorld::RefCollection& references, const CSMWorld::RefIdCollection& referencables,
            const CSMWorld::IdCollection<CSMWorld::Cell>& cells, const CSMWorld::IdCollection<ESM::Faction>& factions,
            const CSMWorld::IdCollection<ESM::BodyPart>& bodyparts);

        void perform(int stage, CSMDoc::Messages& messages) override;
        int setup() override;

    private:
        const CSMWorld::RefCollection& mReferences;
        const CSMWorld::RefIdCollection& mObjects;
        const CSMWorld::RefIdData& mDataSet;
        const CSMWorld::IdCollection<CSMWorld::Cell>& mCells;
        const CSMWorld::IdCollection<ESM::Faction>& mFactions;
        const CSMWorld::IdCollection<ESM::BodyPart>& mBodyParts;
        std::unordered_map<ESM::RefNum, ESM::RefId> mUsedReferenceIDs;
        bool mIgnoreBaseRecords;
    };
}
