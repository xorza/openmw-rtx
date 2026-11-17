#pragma once

#include <cstdint>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class Resources;
}

namespace ESM
{
    struct BirthSign;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that birthsign records are internally consistent
    class BirthsignCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::BirthSign>& mBirthsigns;
        const CSMWorld::Resources& mTextures;
        bool mIgnoreBaseRecords;

    public:
        BirthsignCheckStage(
            const CSMWorld::IdCollection<ESM::BirthSign>& birthsigns, const CSMWorld::Resources& textures);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
