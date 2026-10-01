#include "thread.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include <components/misc/windows.hpp>

namespace Platform
{
    void nameThisThread(const std::string_view name)
    {
        const int length = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), length);
        SetThreadDescription(GetCurrentThread(), wide.c_str());
    }

    std::string nameOfThisThread()
    {
        PWSTR wide = nullptr;
        if (FAILED(GetThreadDescription(GetCurrentThread(), &wide)))
            return {};

        const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        std::string name(static_cast<std::size_t>(length > 0 ? length - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, name.data(), length, nullptr, nullptr);
        LocalFree(wide);
        return name;
    }
}
