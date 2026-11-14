#pragma once

#include <map>
#include <string>

namespace VFS
{
    class File;

    namespace Path
    {
        class Normalized;
    }

    using FileMap = std::map<Path::Normalized, File*, std::less<>>;
}
