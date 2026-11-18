#pragma once

#include <components/esm3/locals.hpp>

#include "importscri.hpp"

namespace ESSImport
{

    /// Convert script variable assignments
    void convertSCRI(const SCRI& scri, ESM::Locals& locals);

}
