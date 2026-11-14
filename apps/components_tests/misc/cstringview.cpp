#include <string>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include <components/misc/cstringview.hpp>

namespace Misc
{
    namespace
    {
        // Only a literal or a string that outlives the view makes one: a bare pointer says nothing of its
        // terminator, and a temporary string would be gone before the view is read.
        static_assert(!std::is_constructible_v<CStringView, const char*>);
        static_assert(!std::is_constructible_v<CStringView, std::string&&>);
        static_assert(std::is_constructible_v<CStringView, const std::string&>);

        /// A literal's view is its text without the terminator, and the C string is that text followed by the
        /// nought.
        TEST(MiscCStringViewTest, aLiteralIsItsTextAndEndsInANought)
        {
            constexpr CStringView named = "openmw-notes";
            const std::string_view text = named;
            EXPECT_EQ(text, "openmw-notes");
            EXPECT_EQ(text.size(), 12u);
            EXPECT_EQ(named.c_str(), text.data());
            EXPECT_EQ(named.c_str()[12], '\0');

            constexpr CStringView empty = "";
            EXPECT_EQ(std::string_view(empty).size(), 0u);
            EXPECT_EQ(empty.c_str()[0], '\0');
        }

        /// A string's view reads the string's own bytes, so a change to the string is what the view says.
        TEST(MiscCStringViewTest, aStringsViewIsTheStringsBytes)
        {
            std::string title = "OpenMW RTX";
            title.reserve(64);
            const CStringView view = title;
            EXPECT_EQ(view.c_str(), title.c_str());
            EXPECT_EQ(view, "OpenMW RTX");
            EXPECT_FALSE(view == "OpenMW") << "a prefix is not the text";
            EXPECT_FALSE(view == "OpenMW RTX ") << "nor is the text with more after it";
        }
    }
}
