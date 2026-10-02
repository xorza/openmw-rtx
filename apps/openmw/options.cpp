#include "options.hpp"

#include <components/files/configurationmanager.hpp>
#include <components/files/installationoptions.hpp>
#include <components/misc/rng.hpp>

namespace
{
    namespace bpo = boost::program_options;
}

namespace OpenMW
{
    bpo::options_description makeOptionsDescription()
    {
        bpo::options_description desc("Syntax: openmw <options>\nAllowed options");
        Files::ConfigurationManager::addCommonOptions(desc);
        Files::addInstallationOptions(desc);

        auto addOption = desc.add_options();
        addOption("help", "print help message");
        addOption("version", "print version information and quit");

        addOption("start", bpo::value<std::string>()->default_value(""), "set initial cell");

        addOption("no-sound", bpo::value<bool>()->implicit_value(true)->default_value(false), "disable all sounds");

        addOption("script-all", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "compile all scripts (excluding dialogue scripts) at startup");

        addOption("script-all-dialogue", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "compile all dialogue scripts at startup");

        addOption("script-console", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "enable console-only script functionality");

        addOption("script-run", bpo::value<std::string>()->default_value(""),
            "select a file containing a list of console commands that is executed on startup");

        addOption("script-warn", bpo::value<int>()->implicit_value(1)->default_value(1),
            "handling of warnings when compiling scripts\n"
            "\t0 - ignore warnings\n"
            "\t1 - show warnings but consider script as correctly compiled anyway\n"
            "\t2 - treat warnings as errors");

        addOption("load-savegame", bpo::value<Files::MaybeQuotedPath>()->default_value(Files::MaybeQuotedPath(), ""),
            "load a save game file on game startup (specify an absolute filename or a filename relative to the current "
            "working directory)");

        addOption("skip-menu", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "skip main menu on game startup");

        addOption("new-game", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "run new game sequence (ignored if skip-menu=0)");

        addOption("no-grab", bpo::value<bool>()->implicit_value(true)->default_value(false), "Don't grab mouse cursor");

        addOption("export-fonts", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "Export Morrowind .fnt fonts to PNG image and XML file in current directory");

        addOption("activate-dist", bpo::value<int>()->default_value(-1), "activation distance override");

        addOption("random-seed", bpo::value<unsigned int>()->default_value(Misc::Rng::generateDefaultSeed()),
            "seed value for random number generator");

        return desc;
    }
}
