#pragma once

#include <components/esm3/globalscript.hpp>

#include "importscpt.hpp"

namespace ESSImport
{

    void convertSCPT(const SCPT& scpt, ESM::GlobalScript& out);

}
