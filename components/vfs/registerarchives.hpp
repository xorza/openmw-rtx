#pragma once

#include <components/files/collections.hpp>

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace VFS
{
    class Manager;

    /// @brief Register BSA and file system archives based on the given OpenMW configuration.
    void registerArchives(VFS::Manager* vfs, const Files::Collections& collections,
        const std::vector<std::string>& archives, bool useLooseFiles, const ToUTF8::StatelessUtf8Encoder* encoder);
}
