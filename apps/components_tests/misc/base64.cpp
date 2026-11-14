#include <string>

#include <gtest/gtest.h>

#include <Base64.h>

namespace
{
    /// **Every length round-trips, the ones under a block of three included**: an empty string, one byte and two
    /// encode to nothing, one block padded twice and one padded once, where the loop's bound `in_len - 2` wrapped
    /// for the first two and read past the input.
    TEST(Base64Test, everyLengthRoundTrips)
    {
        EXPECT_EQ(Base64::Base64::Encode(""), "");
        EXPECT_EQ(Base64::Base64::Encode("a"), "YQ==");
        EXPECT_EQ(Base64::Base64::Encode("ab"), "YWI=");
        EXPECT_EQ(Base64::Base64::Encode("abc"), "YWJj");
        EXPECT_EQ(Base64::Base64::Encode("abcd"), "YWJjZA==");

        for (const std::string text : { "", "a", "ab", "abc", "abcd", "[Video]\ngamma = 1.25\n" })
        {
            std::string decoded;
            EXPECT_EQ(Base64::Base64::Decode(Base64::Base64::Encode(text), decoded), "") << text;
            EXPECT_EQ(decoded, text);
        }
    }
}
