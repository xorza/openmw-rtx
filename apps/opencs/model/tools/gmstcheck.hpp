#pragma once

#include <cstdint>
#include <string>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <components/esm3/variant.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    struct GameSetting;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that GMSTs are alright
    class GmstCheckStage : public CSMDoc::Stage
    {
    public:
        explicit GmstCheckStage(const CSMWorld::IdCollection<ESM::GameSetting>& gameSettings);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages

    private:
        const CSMWorld::IdCollection<ESM::GameSetting>& mGameSettings;
        bool mIgnoreBaseRecords;

        std::string varTypeToString(ESM::VarType);
    };
}
