#include "filesync.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace Platform
{
    bool syncFile(const std::filesystem::path& path)
    {
        const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor < 0)
            return false;

        const bool synced = fsync(descriptor) == 0;
        close(descriptor);
        return synced;
    }
}
