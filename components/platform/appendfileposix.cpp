#include "appendfile.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <unistd.h>

namespace Platform
{
    AppendFile AppendFile::open(const std::filesystem::path& path, const AppendStart start)
    {
        AppendFile file;
        const bool emptied = start == AppendStart::Emptied;
        const std::int32_t flags = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | (emptied ? O_TRUNC : 0);
        file.mHold = UniqueHold<Closing>(::open(path.c_str(), flags, 0644));
        return file;
    }

    void AppendFile::Closing::close(const Handle hold) noexcept
    {
        ::close(static_cast<std::int32_t>(hold));
    }

    bool AppendFile::write(std::string_view bytes) const
    {
        while (isOpen() && !bytes.empty())
        {
            const ssize_t written = ::write(static_cast<std::int32_t>(mHold.get()), bytes.data(), bytes.size());
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                return false;
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
        return isOpen();
    }
}
