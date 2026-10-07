#include "appendfile.hpp"

#include <cerrno>

#include <fcntl.h>
#include <unistd.h>

namespace Platform
{
    AppendFile AppendFile::open(const std::filesystem::path& path, const bool emptied)
    {
        AppendFile file;
        const int flags = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | (emptied ? O_TRUNC : 0);
        file.mHold = ::open(path.c_str(), flags, 0644);
        return file;
    }

    AppendFile::~AppendFile()
    {
        if (isOpen())
            close(static_cast<int>(mHold));
    }

    void AppendFile::write(std::string_view bytes) const
    {
        while (isOpen() && !bytes.empty())
        {
            const ssize_t written = ::write(static_cast<int>(mHold), bytes.data(), bytes.size());
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                return;
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
    }
}
