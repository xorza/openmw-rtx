#pragma once

#include <string>

namespace ESM
{
    class ESMReader;
}

namespace ESSImport
{

    /// Journal
    struct JOUR
    {
        // The entire journal, in HTML
        std::string mText;

        void load(ESM::ESMReader& esm);
    };

}
