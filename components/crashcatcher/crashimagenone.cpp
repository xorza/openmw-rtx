#include "crashinstall.hpp"

#include <string_view>

// No AppImage outside Linux, and so no mount for a monitor to outlive.
namespace Crash
{
    std::string_view keepImageMounted()
    {
        return {};
    }
}
