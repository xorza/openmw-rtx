#include <string_view>

#include "crashinstall.hpp"

// No AppImage outside Linux, and no monitor to outlive one in a build with no Crashpad: no mount
// to keep.
namespace Crash
{
    std::string_view keepImageMounted()
    {
        return {};
    }
}
