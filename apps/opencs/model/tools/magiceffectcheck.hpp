#pragma once

#include <cstdint>
#include <string>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/esm3/loadsoun.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class RefIdCollection;
    class Resources;
}

namespace ESM
{
    struct MagicEffect;
    struct Sound;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that magic effect records are internally consistent
    class MagicEffectCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::MagicEffect>& mMagicEffects;
        const CSMWorld::IdCollection<ESM::Sound>& mSounds;
        const CSMWorld::RefIdCollection& mObjects;
        const CSMWorld::Resources& mIcons;
        const CSMWorld::Resources& mTextures;
        bool mIgnoreBaseRecords;

    private:
        std::string checkObject(
            const ESM::RefId& id, const CSMWorld::UniversalId& type, const std::string& column) const;

    public:
        MagicEffectCheckStage(const CSMWorld::IdCollection<ESM::MagicEffect>& effects,
            const CSMWorld::IdCollection<ESM::Sound>& sounds, const CSMWorld::RefIdCollection& objects,
            const CSMWorld::Resources& icons, const CSMWorld::Resources& textures);

        std::int32_t setup() override;
        ///< \return number of steps
        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
