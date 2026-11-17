#include "convertscri.hpp"

#include <cstdint>

namespace
{

    template <typename T, ESM::VarType VariantType>
    void storeVariables(const std::vector<T>& variables, ESM::Locals& locals, const std::string& scriptname)
    {
        for (const auto& variable : variables)
        {
            ESM::Variant val(variable);
            val.setType(VariantType);
            locals.mVariables.emplace_back(std::string(), val);
        }
    }

}

namespace ESSImport
{

    void convertSCRI(const SCRI& scri, ESM::Locals& locals)
    {
        // order *is* important, as we do not have variable names available in this format
        storeVariables<std::int16_t, ESM::VT_Short>(scri.mShorts, locals, scri.mScript);
        storeVariables<std::int32_t, ESM::VT_Int>(scri.mLongs, locals, scri.mScript);
        storeVariables<float, ESM::VT_Float>(scri.mFloats, locals, scri.mScript);
    }

}
