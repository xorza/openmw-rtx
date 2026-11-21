#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/any.hpp>
#include <boost/program_options/errors.hpp>

#include <components/fallback/validate.hpp>

namespace
{
    Fallback::FallbackMap parse(const std::vector<std::string>& tokens)
    {
        boost::any parsed;
        Fallback::validate(parsed, tokens, nullptr, 0);
        return boost::any_cast<Fallback::FallbackMap>(parsed);
    }

    /// **A key takes everything after its first comma**, commas within a colour among it, and an empty value is a
    /// value: Morrowind.ini's blank movie lines import as one, and refusing it ended the game at its start (upstream
    /// #9213).
    TEST(FallbackValidateTest, aKeyTakesWhatFollowsItsFirstCommaAnEmptyValueAmongThem)
    {
        const Fallback::FallbackMap map = parse(
            { "Movies_New_Game,", "Movies_Company_Logo,bethesda logo.bik", "Weather_Clear_Sky_Day_Color,200,215,255" });
        EXPECT_EQ(map.mMap,
            (std::map<std::string, std::string>{ { "Movies_Company_Logo", "bethesda logo.bik" },
                { "Movies_New_Game", "" }, { "Weather_Clear_Sky_Day_Color", "200,215,255" } }));
    }

    /// The last value a key is given stands, an empty one as well as any.
    TEST(FallbackValidateTest, theLastValueOfAKeyStands)
    {
        EXPECT_EQ(parse({ "Movies_New_Game,new game.bik", "Movies_New_Game," }).mMap.at("Movies_New_Game"), "");
        EXPECT_EQ(
            parse({ "Movies_New_Game,", "Movies_New_Game,new game.bik" }).mMap.at("Movies_New_Game"), "new game.bik");
    }

    /// A token with no key before its comma, or with no comma, names nothing, and is refused.
    TEST(FallbackValidateTest, aTokenWithNoKeyOrNoCommaIsRefused)
    {
        for (const std::string token : { ",new game.bik", "Movies_New_Game", "" })
        {
            SCOPED_TRACE(token);
            EXPECT_THROW(parse({ token }), boost::program_options::validation_error);
        }
    }
}
