#pragma once

#include <cstdint>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/infocollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class InfoCollection;
}

namespace ESM
{
    struct Dialogue;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that journal infos are good
    class JournalCheckStage : public CSMDoc::Stage
    {
    public:
        JournalCheckStage(
            const CSMWorld::IdCollection<ESM::Dialogue>& journals, const CSMWorld::InfoCollection& journalInfos);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages

    private:
        const CSMWorld::IdCollection<ESM::Dialogue>& mJournals;
        const CSMWorld::InfoCollection& mJournalInfos;
        bool mIgnoreBaseRecords;
        CSMWorld::InfosRecordPtrByTopic mInfosByTopic;
    };
}
