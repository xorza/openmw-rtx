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
    struct Sound;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that sound records are internally consistent
    class SoundCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Sound>& mSounds;
        const CSMWorld::Resources& mSoundFiles;
        bool mIgnoreBaseRecords;

    public:
        SoundCheckStage(const CSMWorld::IdCollection<ESM::Sound>& sounds, const CSMWorld::Resources& soundfiles);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
