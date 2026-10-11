#include "userhome.hpp"

#include <cstddef>
#include <cstdlib>
#include <vector>

#include <pwd.h>
#include <unistd.h>

namespace Platform
{
    std::optional<std::filesystem::path> userHome()
    {
        if (const char* const home = std::getenv("HOME"); home != nullptr && *home != '\0')
            return std::filesystem::path(home);

        // The reentrant form, because the crash monitor asks from a thread of its own.
        const long suggested = sysconf(_SC_GETPW_R_SIZE_MAX);
        std::vector<char> buffer(suggested > 0 ? static_cast<std::size_t>(suggested) : 16384);
        passwd entry{};
        passwd* found = nullptr;
        if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) != 0 || found == nullptr
            || found->pw_dir == nullptr || *found->pw_dir == '\0')
            return std::nullopt;
        return std::filesystem::path(found->pw_dir);
    }
}
