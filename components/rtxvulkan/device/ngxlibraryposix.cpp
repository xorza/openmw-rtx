#include "ngxlibrary.hpp"

#include <array>

namespace Rtx::Dlss::Ngx
{
    Platform::Library::ScopedHandle openCore()
    {
        constexpr std::array<const char*, 2> sNames{ "libnvidia-ngx.so.1", "libnvidia-ngx.so" };
        for (const char* name : sNames)
        {
            Platform::Library::ScopedHandle core(Platform::Library::open(name));
            if (core.isOpen())
                return core;
        }

        return {};
    }
}
