#include "locals.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{

    void Locals::load(ESMReader& esm)
    {
        while (esm.isNextSub("LOCA"))
        {
            std::string id = esm.getHString();

            Variant value;
            value.read(esm, Variant::Format_Local);

            mVariables.emplace_back(id, value);
        }
    }

    void Locals::save(ESMWriter& esm) const
    {
        for (const auto& variable : mVariables)
        {
            esm.writeHNString("LOCA", variable.first);
            variable.second.write(esm, Variant::Format_Local);
        }
    }

}
