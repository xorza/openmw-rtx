#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <string_view>
#include <type_traits>

#include <components/misc/strings/conversion.hpp>

namespace RtxTool
{
    /// The number the whole of `text` spells, by `Misc::StringUtils::toNumeric`'s rules: nothing where
    /// anything but the number is in it, as a value somebody typed for one field is read, and a
    /// floating-point number only where it is finite.
    template <typename T>
    std::optional<T> wholeNumber(std::string_view text)
    {
        if constexpr (std::is_floating_point_v<T> && !Misc::StringUtils::sFromCharsReadsFloats)
        {
            if (Misc::StringUtils::floatPrefix(text) != text.size())
                return std::nullopt;
        }
        else
        {
            T result{};
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
            if (ec != std::errc() || ptr != text.data() + text.size())
                return std::nullopt;
        }

        return Misc::StringUtils::toNumeric<T>(text);
    }

    /// The integer the whole of `text` spells in `base`, in that base's digits and nothing else: no
    /// sign an unsigned type cannot hold, no prefix, and nothing after the last digit.
    template <std::integral T>
    std::optional<T> wholeNumber(std::string_view text, int base)
    {
        T result{};
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), result, base);
        if (ec != std::errc() || ptr != text.data() + text.size())
            return std::nullopt;
        return result;
    }
}
