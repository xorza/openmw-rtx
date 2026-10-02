#ifndef COMPONENTS_MISC_STRINGS_CONVERSION_H
#define COMPONENTS_MISC_STRINGS_CONVERSION_H

#include <charconv>
#include <cmath>
#include <cstdint>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

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

    // support for std::from_chars as of 2023-02-27
    // - Visual Studio 2019 version 16.4 (1924)
    // - GCC 11
    // - Clang does not support floating points yet
    // - Apples Clang does not support floating points yet
#if !(defined(_MSC_VER) && (_MSC_VER >= 1924)) && !(defined(__GNUC__) && __GNUC__ >= 11) || defined(__clang__)         \
    || defined(__apple_build_version__)
    inline constexpr bool sFromCharsReadsFloats = false;
#else
    inline constexpr bool sFromCharsReadsFloats = true;
#endif

    /// The number `s` spells, or nothing where it spells none. A floating-point number is a finite
    /// one: `std::from_chars` reads `inf` and `nan` as numbers, and no text this reads — a setting,
    /// a fallback, a script's literal — means either, which every reader would then carry into its
    /// arithmetic unseen.
    /// The floating-point number `s` begins with, read by a classic-locale stream: what `toNumeric`
    /// reads with where `std::from_chars` has no floating point. **Held to the prefix `from_chars`
    /// reads**, an optional minus, digits with an optional fraction and a complete exponent, so a
    /// spelling is the same number on every toolchain or none on any: a stream on its own skips
    /// leading whitespace, reads a leading `+`, and on libc++ reads `0x10` as sixteen.
    template <typename T>
    inline std::optional<T> toFloatByStream(std::string_view s)
    {
        const auto digitAt = [&](std::size_t at) { return at < s.size() && s[at] >= '0' && s[at] <= '9'; };

        std::size_t at = s.starts_with('-') ? 1 : 0;
        const std::size_t whole = at;
        while (digitAt(at))
            ++at;
        bool read = at > whole;
        if (at < s.size() && s[at] == '.' && (read || digitAt(at + 1)))
        {
            ++at;
            read = read || digitAt(at);
            while (digitAt(at))
                ++at;
        }
        if (!read)
            return std::nullopt;

        if (at < s.size() && (s[at] == 'e' || s[at] == 'E'))
        {
            std::size_t exponent = at + 1;
            if (exponent < s.size() && (s[exponent] == '+' || s[exponent] == '-'))
                ++exponent;
            if (digitAt(exponent))
            {
                at = exponent;
                while (digitAt(at))
                    ++at;
            }
        }

        T result{};
        std::istringstream stream{ std::string(s.substr(0, at)) };
        stream.imbue(std::locale::classic());
        if (!(stream >> result))
            return std::nullopt;

        return result;
    }

    template <typename T>
    inline std::optional<T> toNumeric(std::string_view s)
    {
        T result{};
        if constexpr (std::is_floating_point_v<T> && !sFromCharsReadsFloats)
        {
            const std::optional<T> read = toFloatByStream<T>(s);
            if (!read.has_value())
                return std::nullopt;
            result = *read;
        }
        else
        {
            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
            if (ec != std::errc())
                return std::nullopt;
        }

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
