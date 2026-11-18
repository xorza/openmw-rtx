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
    struct Faction;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that faction records are internally consistent
    class FactionCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Faction>& mFactions;
        bool mIgnoreBaseRecords;

    public:
        explicit FactionCheckStage(const CSMWorld::IdCollection<ESM::Faction>& factions);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
