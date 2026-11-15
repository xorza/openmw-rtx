#include "importer.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/toutf8/toutf8.hpp>

namespace bpo = boost::program_options;

int main(int argc, char* argv[])
{
    try
    {
        bpo::options_description desc("Syntax: openmw-iniimporter <options> inifile configfile\nAllowed options");
        bpo::positional_options_description positionalDesc;
        auto addOption = desc.add_options();
        addOption("help,h", "produce help message");
        addOption("verbose,v", "verbose output");
        addOption("ini,i", bpo::value<Files::MaybeQuotedPath>(), "morrowind.ini file");
        addOption("cfg,c", bpo::value<Files::MaybeQuotedPath>(), "openmw.cfg file");
        addOption("output,o", bpo::value<Files::MaybeQuotedPath>()->default_value({}), "openmw.cfg file");
        addOption("game-files,g", "import esm and esp files");
        addOption("fonts,f", "import bitmap fonts");
        addOption("no-archives,A", "disable bsa archives import");
        addOption("encoding,e", bpo::value<std::string>()->default_value("win1252"),
            "Character encoding used in OpenMW game messages:\n"
            "\n\twin1250 - Central and Eastern European such as Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, "
            "Croatian, Serbian (Latin script), Romanian and Albanian languages\n"
            "\n\twin1251 - Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic and other languages\n"
            "\n\twin1252 - Western European (Latin) alphabet, used by default");
        ;
        positionalDesc.add("ini", 1).add("cfg", 1);

        bpo::variables_map vm;

        // The command line as UTF-8, which `argv` is not on Windows, and without the program's name, as the parser
        // takes it.
        std::vector<std::string> arguments = Platform::Process::commandLine(argc, argv);
        if (!arguments.empty())
            arguments.erase(arguments.begin());
        bpo::parsed_options parsed = bpo::command_line_parser(arguments).options(desc).positional(positionalDesc).run();
        bpo::store(parsed, vm);

        if (vm.contains("help") || !vm.contains("ini") || !vm.contains("cfg"))
        {
            std::cout << desc;
            return 0;
        }

        bpo::notify(vm);

        std::filesystem::path iniFile(
            vm["ini"].as<Files::MaybeQuotedPath>().u8string()); // This call to u8string is redundant, but required to
                                                                // build on MSVC 14.26 due to implementation bugs.
        std::filesystem::path cfgFile(
            vm["cfg"].as<Files::MaybeQuotedPath>().u8string()); // This call to u8string is redundant, but required to
                                                                // build on MSVC 14.26 due to implementation bugs.

        // if no output is given, write back to cfg file
        std::filesystem::path outputFile = vm["output"]
                                               .as<Files::MaybeQuotedPath>()
                                               .u8string(); // This call to u8string is redundant, but required to build
                                                            // on MSVC 14.26 due to implementation bugs.
        if (vm["output"].defaulted())
        {
            outputFile = vm["cfg"]
                             .as<Files::MaybeQuotedPath>()
                             .u8string(); // This call to u8string is redundant, but required to build on MSVC 14.26 due
                                          // to implementation bugs.
        }

        if (!std::filesystem::exists(iniFile))
        {
            std::cerr << "ini file does not exist" << std::endl;
            return -3;
        }
        if (!std::filesystem::exists(cfgFile))
            std::cerr << "cfg file does not exist" << std::endl;

        MwIniImporter importer;
        importer.setVerbose(vm.contains("verbose"));

        MwIniImporter::multistrmap cfg = importer.loadCfgFile(cfgFile);

        // Font encoding settings
        std::string encoding;
        if (vm["encoding"].defaulted() && cfg.contains("encoding") && !cfg["encoding"].empty())
            encoding = cfg["encoding"].back();
        else
        {
            encoding = vm["encoding"].as<std::string>();
            cfg["encoding"] = { encoding };
        }
        importer.setInputEncoding(ToUTF8::calculateEncoding(encoding));

        MwIniImporter::multistrmap ini = importer.loadIniFile(iniFile);

        if (!vm.contains("fonts"))
        {
            ini.erase("Fonts:Font 0");
            ini.erase("Fonts:Font 1");
            ini.erase("Fonts:Font 2");
        }

        importer.merge(cfg, ini);
        importer.mergeFallback(cfg, ini);

        if (vm.contains("game-files"))
        {
            importer.importGameFiles(cfg, ini, iniFile);
        }

        if (!vm.contains("no-archives"))
        {
            importer.importArchives(cfg, ini);
        }

        std::cout << "write to: " << Files::pathToUnicodeString(outputFile) << std::endl;
        std::ofstream file(outputFile);
        importer.writeToFile(file, cfg);
    }
    catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
    }
    return 0;
}
