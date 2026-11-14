#pragma once

#include "importscri.hpp"

#include <components/esm3/locals.hpp>

namespace ESSImport
{

    /// Convert script variable assignments
    void convertSCRI(const SCRI& scri, ESM::Locals& locals);

}
