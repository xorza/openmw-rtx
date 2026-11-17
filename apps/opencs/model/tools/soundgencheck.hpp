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
    class RefIdCollection;
}

namespace ESM
{
    struct SoundGenerator;
    struct Sound;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that sound gen records are internally consistent
    class SoundGenCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::SoundGenerator>& mSoundGens;
        const CSMWorld::IdCollection<ESM::Sound>& mSounds;
        const CSMWorld::RefIdCollection& mObjects;
        bool mIgnoreBaseRecords;

    public:
        SoundGenCheckStage(const CSMWorld::IdCollection<ESM::SoundGenerator>& soundGens,
            const CSMWorld::IdCollection<ESM::Sound>& sounds, const CSMWorld::RefIdCollection& objects);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };
}
