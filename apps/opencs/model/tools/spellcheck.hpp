#pragma once

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    struct Spell;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that spell records are internally consistent
    class SpellCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Spell>& mSpells;
        bool mIgnoreBaseRecords;

    public:
        explicit SpellCheckStage(const CSMWorld::IdCollection<ESM::Spell>& spells);

        int setup() override;
        ///< \return number of steps

        void perform(int stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
