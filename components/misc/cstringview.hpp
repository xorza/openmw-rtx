#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Misc
{
    /// A view of text that ends in a nought, for a C API that reads up to one: `std::string_view` with the
    /// terminator its type promises, which `std::string_view::data()` does not. The shape of P3655's
    /// `std::cstring_view`, which targets C++29, and of Chromium's `base::cstring_view`.
    ///
    /// Made from a literal or a `std::string` only, the two whose terminator is known, and converted to
    /// `std::string_view` wherever text is read as text.
    class CStringView
    {
    public:
        /// A literal, refused where a nought stands inside it, which a C API would stop at short of the view.
        template <std::size_t size>
        // Implicit, as `std::string_view`'s from a literal is.
        // NOLINTNEXTLINE(google-explicit-constructor)
        consteval CStringView(const char (&literal)[size])
            : mView(literal, size - 1)
        {
            if (mView.find('\0') != std::string_view::npos || literal[size - 1] != '\0')
                // A plain value thrown on purpose, which stops the compile.
                // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
                throw "a CStringView's literal holds a nought before its end";
        }

        /// A string's text, which `std::string` keeps terminated.
        // Implicit, as `std::string_view`'s from a string is.
        // NOLINTNEXTLINE(google-explicit-constructor)
        CStringView(const std::string& string) noexcept
            : mView(string)
        {
        }

        /// Refused: the view would outlive the string.
        CStringView(std::string&&) = delete;

        /// The text with its terminator, for a C API.
        constexpr const char* c_str() const noexcept { return mView.data(); }

        // Implicit, so a `CStringView` goes wherever text is read as text.
        // NOLINTNEXTLINE(google-explicit-constructor)
        constexpr operator std::string_view() const noexcept { return mView; }

        friend constexpr bool operator==(const CStringView left, const std::string_view right) noexcept
        {
            return left.mView == right;
        }

    private:
        std::string_view mView;
    };
}
