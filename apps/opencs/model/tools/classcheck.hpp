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
    struct Class;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that class records are internally consistent
    class ClassCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Class>& mClasses;
        bool mIgnoreBaseRecords;

    public:
        explicit ClassCheckStage(const CSMWorld::IdCollection<ESM::Class>& classes);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
