#pragma once

#include <apps/opencs/model/doc/stage.hpp>

#include "search.hpp"

namespace CSMDoc
{
    class Messages;
}

namespace CSMWorld
{
    class IdTableBase;
}

namespace CSMTools
{
    class SearchOperation;

    class SearchStage : public CSMDoc::Stage
    {
        const CSMWorld::IdTableBase* mModel;
        Search mSearch;
        const SearchOperation* mOperation;

    public:
        explicit SearchStage(const CSMWorld::IdTableBase* model);

        int setup() override;
        ///< \return number of steps

        void perform(int stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.

        void setOperation(const SearchOperation* operation);
    };
}
