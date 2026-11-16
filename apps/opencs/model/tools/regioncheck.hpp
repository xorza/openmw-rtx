#pragma once

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    struct Region;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that region records are internally consistent
    class RegionCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Region>& mRegions;
        bool mIgnoreBaseRecords;

    public:
        explicit RegionCheckStage(const CSMWorld::IdCollection<ESM::Region>& regions);

        int setup() override;
        ///< \return number of steps

        void perform(int stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
