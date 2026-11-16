#pragma once

#include <string>

#include <components/esm3/cellref.hpp>

#include "importacdt.hpp"

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    struct CellRef : public ESM::CellRef
    {
        std::string mIndexedRefId;

        std::string mScript;

        bool mEnabled{};

        bool mDeleted{};

        ActorData mActorData;

        void load(ESM::ESMReader& esm);

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        ~CellRef() = default;
    };

}
