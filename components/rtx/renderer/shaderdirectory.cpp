#include "shaderdirectory.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/result.hpp>
#include <components/platform/wholefile.hpp>
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
        Misc::ByteBuffer bytes;
        for (const std::filesystem::path& file : files)
        {
            // A module that will not read is left out: a digest is a key, and a module that will
            // not read is one the renderer reading it reports.
            const std::string name = Files::pathToUnicodeString(file.filename());
            if (const Result<void, Platform::IoError> read = Platform::readWhole(file, bytes, Platform::sUnbounded);
                !read.isOk())
            {
                Log(Debug::Warning) << "Rtx: " << name
                                    << " would not read for the shaders' digest: " << read.error().mCode.message();
                continue;
            }

            // The name as well as the contents, so that renaming a shader is a change and two files
            // trading contents is not the same set.
            digest.add(std::span<const char>(name));
            digest.add(std::span<const std::byte>(bytes));
        }

        return digest.getWords();
    }
}
