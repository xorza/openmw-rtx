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
#include <components/misc/hashstate.hpp>
#include <components/misc/result.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/wholefile.hpp>

namespace Rtx
{
    std::filesystem::path shaderDirectory(const std::filesystem::path& resources)
    {
        return resources / "rtx" / "shaders";
    }

    Misc::DigestWords digestShaders(const std::filesystem::path& directory)
    {
        std::vector<std::filesystem::path> files;
        for (const std::filesystem::directory_entry& entry :
            Platform::listFolder(directory).value_or(std::vector<std::filesystem::directory_entry>()))
            if (std::error_code typed; entry.is_regular_file(typed))
                files.push_back(entry.path());

        std::sort(files.begin(), files.end());

        Misc::HashState digest;
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
