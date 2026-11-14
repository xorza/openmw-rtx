#ifndef COMPONENTS_MISC_STRINGS_CONVERSION_H
#define COMPONENTS_MISC_STRINGS_CONVERSION_H

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <version>

namespace Misc::StringUtils
{
    inline const char* u8StringToString(const char8_t* str)
    {
        return reinterpret_cast<const char*>(str);
    }

    inline char* u8StringToString(char8_t* str)
    {
        return reinterpret_cast<char*>(str);
    }

    inline std::string u8StringToString(std::u8string_view str)
    {
        return { str.begin(), str.end() };
    }

    inline std::string u8StringToString(std::u8string&& str)
    {
        return { str.begin(), str.end() };
    }

    inline const char8_t* stringToU8String(const char* str)
    {
        return reinterpret_cast<const char8_t*>(
            str); // Undefined behavior if the contents of "char" aren't UTF8 or ASCII.
    }

    inline char8_t* stringToU8String(char* str)
    {
        return reinterpret_cast<char8_t*>(str); // Undefined behavior if the contents of "char" aren't UTF8 or ASCII.
    }

    inline std::u8string stringToU8String(std::string_view str)
    {
        return { str.begin(), str.end() }; // Undefined behavior if the contents of "char" aren't UTF8 or ASCII.
    }

    inline std::u8string stringToU8String(std::string&& str)
    {
        return { str.begin(), str.end() }; // Undefined behavior if the contents of "char" aren't UTF8 or ASCII.
    }

    // Every toolchain the fork builds with reads floating point through `std::from_chars`, which is
    // what `__cpp_lib_to_chars` says of a library once the whole of `<charconv>` is there.
    static_assert(__cpp_lib_to_chars >= 201611L, "a standard library whose std::from_chars reads no floats");

    /// The number `s` spells, or nothing where it spells none. A floating-point number is a finite
    /// one: `std::from_chars` reads `inf` and `nan` as numbers, and no text this reads — a setting,
    /// a fallback, a script's literal — means either, which every reader would then carry into its
    /// arithmetic unseen.
    template <typename T>
    inline std::optional<T> toNumeric(std::string_view s)
    {
        T result{};
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
        if (ec != std::errc())
            return std::nullopt;

        if constexpr (std::is_floating_point_v<T>)
            if (!std::isfinite(result))
                return std::nullopt;

        return result;
    }

    template <typename T>
    inline T toNumeric(std::string_view s, T defaultValue)
    {
        if (auto numeric = toNumeric<T>(s))
        {
            return *numeric;
        }

        return defaultValue;
    }

    inline std::string toHex(std::string_view value)
    {
        std::string buffer(value.size() * 2, '0');
        char* out = buffer.data();
        for (const char v : value)
        {
            const std::ptrdiff_t space = static_cast<std::ptrdiff_t>(static_cast<std::uint8_t>(v) <= 0xf);
            const auto [ptr, ec] = std::to_chars(out + space, out + space + 2, static_cast<std::uint8_t>(v), 16);
            if (ec != std::errc())
                throw std::system_error(std::make_error_code(ec));
            out += 2;
        }
        return buffer;
    }
}

#endif // COMPONENTS_MISC_STRINGS_CONVERSION_H
