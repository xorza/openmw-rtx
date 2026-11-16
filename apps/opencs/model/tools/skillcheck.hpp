#pragma once

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    struct Skill;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that skill records are internally consistent
    class SkillCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Skill>& mSkills;
        bool mIgnoreBaseRecords;

    public:
        explicit SkillCheckStage(const CSMWorld::IdCollection<ESM::Skill>& skills);

        int setup() override;
        ///< \return number of steps

        void perform(int stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
