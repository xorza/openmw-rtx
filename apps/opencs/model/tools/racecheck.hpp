#pragma once

#include <cstdint>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    struct Race;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that race records are internally consistent
    class RaceCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Race>& mRaces;
        bool mPlayable;
        bool mIgnoreBaseRecords;

        void performPerRecord(std::int32_t stage, CSMDoc::Messages& messages);

        void performFinal(CSMDoc::Messages& messages);

    public:
        explicit RaceCheckStage(const CSMWorld::IdCollection<ESM::Race>& races);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
