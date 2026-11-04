#include "shaderdirectory.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/files/hash.hpp>
#include <components/rtx/common/hashstate.hpp>

namespace Rtx
{
    std::filesystem::path shaderDirectory(const std::filesystem::path& resources)
    {
        return resources / "rtx" / "shaders";
    }

    DigestWords digestShaders(const std::filesystem::path& directory)
    {
        // **Every step of the walk told its error, and none thrown**: the range-for advances with
        // `operator++`, which throws on a directory that went or changed under the walk, where
        // `increment` reports it, as a file it cannot read is reported and skipped below.
        std::error_code failed;
        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator entry(directory, failed);
             !failed && entry != std::filesystem::directory_iterator(); entry.increment(failed))
            if (std::error_code typed; entry->is_regular_file(typed))
                files.push_back(entry->path());

        std::sort(files.begin(), files.end());

        HashState digest;
        for (const std::filesystem::path& file : files)
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
                continue;

            // The name as well as the contents, so that renaming a shader is a change and two files
            // trading contents is not the same set.
            const std::string name = Files::pathToUnicodeString(file.filename());
            digest.add(std::span<const char>(name));

            // `Files::getHash` throws where a read fails, and this may not: a digest is a key, and a
            // module that will not read is one the renderer reading it reports.
            try
            {
                digest.add(Files::getHash(name, stream));
            }
            catch (const std::exception& error)
            {
                Log(Debug::Warning) << "Rtx: " << name << " would not read for the shaders' digest: " << error.what();
            }
        }

        return digest.getWords();
    }
}
