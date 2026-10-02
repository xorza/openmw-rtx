#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/openmw/options.hpp>
#include <apps/openmw/startup.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/testing/util.hpp>

namespace
{
    namespace bpo = boost::program_options;

    Misc::Result<OpenMW::Installation, std::string> readFrom(
        const std::vector<std::string>& arguments, const OpenMW::InstallationExtras& extras)
    {
        std::vector<const char*> line{ "openmw" };
        for (const std::string& argument : arguments)
            line.push_back(argument.c_str());

        bpo::options_description description = OpenMW::makeOptionsDescription();
        bpo::variables_map variables;
        Files::parseArgs(static_cast<int>(line.size()), line.data(), variables, description);
        bpo::notify(variables);

        const Files::ConfigurationManager config;
        return OpenMW::readInstallation(variables, config, extras);
    }

    /// **One reading of an installation for the game and the harness.** The data folders that
    /// exist, the local one last and the extras' after it whether or not they exist yet; the
    /// content after `builtin.omwscripts` and the extras' content; the archives, the groundcover,
    /// the encoding and the fallback values as the line gives them. A content list that is empty,
    /// or names a file twice — the extras' included — is no installation, and says so.
    TEST(RtxInstallationTest, anInstallationIsReadByOneRuleForEveryBinary)
    {
        const std::filesystem::path data = TestingOpenMW::outputDirPath("installation-data");
        const std::filesystem::path local = TestingOpenMW::outputDirPath("installation-local");
        const std::filesystem::path missing = data / "missing";
        const std::filesystem::path keys = data / "keys";
        std::filesystem::remove_all(missing);

        const OpenMW::InstallationExtras extras{ .mDataDirs = { keys }, .mContent = { "rtxtool.omwscripts" } };
        const std::vector<std::string> installed{ "--data=" + Files::pathToUnicodeString(data),
            "--data=" + Files::pathToUnicodeString(missing), "--data-local=" + Files::pathToUnicodeString(local),
            "--content=Morrowind.esm", "--content=Tribunal.esm", "--groundcover=Grass.esp",
            "--fallback-archive=Morrowind.bsa", "--encoding=win1251",
            "--fallback=Weather_Clear_Transition_Delta,0.015" };

        const Misc::Result<OpenMW::Installation, std::string> read = readFrom(installed, extras);
        ASSERT_TRUE(read.isOk()) << read.error();
        const OpenMW::Installation& installation = read.value();
        EXPECT_EQ(installation.mDataDirs, (Files::PathContainer{ data, local, keys }));
        EXPECT_EQ(installation.mContent,
            (std::vector<std::string>{ "builtin.omwscripts", "rtxtool.omwscripts", "Morrowind.esm", "Tribunal.esm" }));
        EXPECT_EQ(installation.mGroundcover, std::vector<std::string>{ "Grass.esp" });
        EXPECT_EQ(installation.mArchives, std::vector<std::string>{ "Morrowind.bsa" });
        EXPECT_EQ(installation.mEncoding, "win1251");
        EXPECT_EQ(installation.mFallback.mMap.at("Weather_Clear_Transition_Delta"), "0.015");

        EXPECT_EQ(readFrom({}, {}).error(), "No content file given (esm/esp, nor omwgame/omwaddon)");
        EXPECT_EQ(readFrom({ "--content=Morrowind.esm", "--content=Morrowind.esm" }, {}).error(),
            "Content file specified more than once: Morrowind.esm");
        EXPECT_EQ(readFrom({ "--content=rtxtool.omwscripts" }, extras).error(),
            "Content file specified more than once: rtxtool.omwscripts");
    }
}
