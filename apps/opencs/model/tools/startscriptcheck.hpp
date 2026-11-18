#pragma once

#include <cstdint>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <components/esm3/loadscpt.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    class Script;
    struct StartScript;
}

namespace CSMTools
{
    class StartScriptCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::StartScript>& mStartScripts;
        const CSMWorld::IdCollection<ESM::Script>& mScripts;
        bool mIgnoreBaseRecords;

    public:
        StartScriptCheckStage(const CSMWorld::IdCollection<ESM::StartScript>& startScripts,
            const CSMWorld::IdCollection<ESM::Script>& scripts);

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        std::int32_t setup() override;
    };
}
