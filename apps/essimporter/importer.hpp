#pragma once

#include <filesystem>

namespace ESSImport
{

    class Importer
    {
    public:
        Importer(std::filesystem::path essfile, std::filesystem::path outfile, std::string encoding);

        void run();

        void compare();

    private:
        std::filesystem::path mEssFile;
        std::filesystem::path mOutFile;
        std::string mEncoding;
    };

}
