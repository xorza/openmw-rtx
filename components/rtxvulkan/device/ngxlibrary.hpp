#pragma once

#include <components/platform/library.hpp>

namespace Rtx::Dlss::Ngx
{
    /// The driver's NGX core, opened where the system keeps it: the DriverStore the Windows
    /// registry names, or the library the Linux loader finds. Invalid where the machine has none.
    Platform::Library::ScopedHandle openCore();
}
