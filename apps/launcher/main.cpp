
#include <cstdint>

#include <QDir>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/debug/debugging.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/qtconversion.hpp>
#include <components/l10n/qttranslations.hpp>
#include <components/platform/application.hpp>
#include <components/platform/platform.hpp>

#include "maindialog.hpp"

namespace
{
    std::int32_t runLauncher(std::int32_t argc, char* argv[])
    {
        Platform::init();

        boost::program_options::variables_map variables;
        boost::program_options::options_description description;
        Files::ConfigurationManager configurationManager;
        configurationManager.addCommonOptions(description);
        configurationManager.readConfiguration(variables, description, true);

        Debug::setupLogging(configurationManager.getLogPath(), configurationManager.getUserDataPath(), "Launcher");

        try
        {
            Platform::Application app(argc, argv, Platform::Look::Graphite);

            app.setDesktopFileName("org.openmw.launcher");

            QString resourcesPath(".");
            if (!variables["resources"].empty())
            {
                resourcesPath = Files::pathToQString(variables["resources"].as<Files::MaybeQuotedPath>().u8string());
            }

            L10n::installQtTranslations(app, "launcher", resourcesPath);

            Launcher::MainDialog mainWin(configurationManager);

            Launcher::FirstRunDialogResult result = mainWin.showFirstRunDialog();
            if (result == Launcher::FirstRunDialogResultFailure)
                return 0;

            if (result == Launcher::FirstRunDialogResultContinue)
                mainWin.show();

            std::int32_t exitCode = app.exec();

            return exitCode;
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Unexpected exception: " << e.what();
            return 0;
        }
    }
}

int main(std::int32_t argc, char* argv[])
{
    return Debug::wrapApplication(runLauncher, argc, argv, "Launcher");
}
