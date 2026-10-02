#include "installationoptions.hpp"

#include <string>
#include <vector>

#include <boost/program_options/value_semantic.hpp>

#include <components/fallback/validate.hpp>

#include "configurationmanager.hpp"

namespace Files
{
    void addInstallationOptions(boost::program_options::options_description& description)
    {
        namespace bpo = boost::program_options;
        using StringsVector = std::vector<std::string>;

        auto addOption = description.add_options();

        addOption("data",
            bpo::value<MaybeQuotedPathContainer>()
                ->default_value(MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");

        addOption("data-local", bpo::value<MaybeQuotedPath>()->default_value(MaybeQuotedPath(), ""),
            "set local data directory (highest priority)");

        addOption("fallback-archive",
            bpo::value<StringsVector>()->default_value(StringsVector(), "fallback-archive")->multitoken()->composing(),
            "set fallback BSA archives (later archives have higher priority)");

        addOption("content", bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");

        addOption("groundcover",
            bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "groundcover content file(s): esm/esp, or omwgame/omwaddon");

        addOption("encoding", bpo::value<std::string>()->default_value("win1252"),
            "Character encoding used in OpenMW game messages:\n"
            "\n\twin1250 - Central and Eastern European such as Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, "
            "Croatian, Serbian (Latin script), Romanian and Albanian languages\n"
            "\n\twin1251 - Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic and other languages\n"
            "\n\twin1252 - Western European (Latin) alphabet, used by default");

        addOption("fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");
    }
}
