#include "ngxlibrary.hpp"

#include <array>
#include <filesystem>
#include <optional>

#include <components/misc/windows.hpp>

namespace Rtx::Dlss::Ngx
{
    namespace
    {
        /// Where the driver installer wrote the DriverStore folder NGX lives in. It is what the
        /// display driver's service keeps, not a fact this program can derive from the adapter.
        std::optional<std::filesystem::path> driverStore()
        {
            struct Key
            {
                const wchar_t* mKey;
                const wchar_t* mValue;
            };
            constexpr std::array<Key, 3> sKeys{ {
                { L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\Parameters\\NGXCore", L"NGXPath" },
                { L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore", L"NGXPath" },
                { L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath" },
            } };

            for (const Key& key : sKeys)
            {
                std::array<wchar_t, 1024> path{};
                DWORD size = static_cast<DWORD>(path.size() * sizeof(wchar_t));
                // `RRF_RT_REG_SZ` alone: it also takes a `REG_EXPAND_SZ` and expands it, where naming
                // `RRF_RT_REG_EXPAND_SZ` without `RRF_NOEXPAND` fails every read as an invalid parameter.
                if (RegGetValueW(HKEY_LOCAL_MACHINE, key.mKey, key.mValue, RRF_RT_REG_SZ, nullptr, path.data(), &size)
                    == ERROR_SUCCESS)
                    return std::filesystem::path(path.data());
            }

            return std::nullopt;
        }
    }

    Platform::Library::ScopedHandle openCore()
    {
        if (const std::optional<std::filesystem::path> store = driverStore(); store.has_value())
        {
            constexpr std::array<const wchar_t*, 2> sNames{ L"_nvngx.dll", L"nvngx.dll" };
            for (const wchar_t* name : sNames)
            {
                Platform::Library::ScopedHandle core(Platform::Library::open(*store / std::filesystem::path(name)));
                if (core.isOpen())
                    return core;
            }
        }

        // The system's own search, in case the driver copied the core beside it and left no
        // DriverStore key, as a legacy install does.
        constexpr std::array<const char*, 2> sBareNames{ "_nvngx.dll", "nvngx.dll" };
        for (const char* name : sBareNames)
        {
            Platform::Library::ScopedHandle core(Platform::Library::open(name));
            if (core.isOpen())
                return core;
        }

        return {};
    }
}
